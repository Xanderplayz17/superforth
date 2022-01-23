#include <stdlib.h>
#include <math.h>
#include "error.h"
#include "machine.h"

static int64_t longpow(int64_t base, int64_t exp) {
	int64_t result = 1;
	for (;;) {
		if (exp & 1)
			result *= base;
		exp >>= 1;
		if (!exp)
			break;
		base *= base;
	}
	return result;
}

heap_alloc_t* machine_alloc(machine_t* machine, uint16_t req_size, gc_trace_mode_t trace_mode) {
	if (machine->heap_count == machine->heap_alloc_limit)
		PANIC(machine, ERROR_STACK_OVERFLOW);
	heap_alloc_t* heap_alloc;
	if (machine->freed_heap_count) {
		heap_alloc = machine->freed_heap_allocs[--machine->freed_heap_count];
		if(!heap_alloc->reg_with_table)
			machine->heap_allocs[machine->heap_count++] = heap_alloc;
	}
	else {
		heap_alloc = malloc(sizeof(heap_alloc_t));
		PANIC_ON_FAIL(heap_alloc, machine, ERROR_MEMORY);
		machine->heap_allocs[machine->heap_count++] = heap_alloc;
		heap_alloc->reg_with_table = 1;
	}
	heap_alloc->pre_freed = 0;
	heap_alloc->limit = req_size;
	heap_alloc->gc_flag = 0;
	heap_alloc->trace_mode = trace_mode;
	PANIC_ON_FAIL(heap_alloc, machine, ERROR_MEMORY);
	PANIC_ON_FAIL(heap_alloc->registers = malloc(req_size * sizeof(machine_reg_t)), machine, ERROR_MEMORY);
	PANIC_ON_FAIL(heap_alloc->init_stat = calloc(req_size, sizeof(int)), machine, ERROR_MEMORY);
	if (trace_mode == GC_TRACE_MODE_SOME)
		PANIC_ON_FAIL(heap_alloc->trace_stat = malloc(req_size * sizeof(int)), machine, ERROR_MEMORY);
	return heap_alloc;
}

int free_alloc(machine_t* machine, heap_alloc_t* heap_alloc) {
	if (heap_alloc->pre_freed)
		return 1;
	heap_alloc->pre_freed = 1;

	switch (heap_alloc->trace_mode) {
	case GC_TRACE_MODE_ALL:
		for (uint_fast16_t i = 0; i < heap_alloc->limit; i++)
			if (heap_alloc->init_stat[i])
				ESCAPE_ON_FAIL(free_alloc(machine, heap_alloc->registers[i].heap_alloc));
		break;
	case GC_TRACE_MODE_SOME:
		for(uint_fast16_t i = 0; i < heap_alloc->limit; i++)
			if(heap_alloc->init_stat[i] && heap_alloc->trace_stat[i])
				ESCAPE_ON_FAIL(free_alloc(machine, heap_alloc->registers[i].heap_alloc));
		free(heap_alloc->trace_stat);
		break;
	}

	free(heap_alloc->registers);
	free(heap_alloc->init_stat);

	if (machine->freed_heap_count == machine->alloc_freed_heaps) {
		heap_alloc_t** new_freed_heaps = realloc(machine->freed_heap_allocs, (machine->alloc_freed_heaps += 10) * sizeof(heap_alloc_t*));
		PANIC_ON_FAIL(new_freed_heaps, machine, ERROR_MEMORY);
		machine->freed_heap_allocs = new_freed_heaps;
	}
	machine->freed_heap_allocs[machine->freed_heap_count++] = heap_alloc;
	return 1;
}

static uint16_t machine_heap_trace(machine_t* machine, heap_alloc_t* heap_alloc, heap_alloc_t** reset_stack, uint16_t* reset_count) {
	if (heap_alloc->gc_flag)
		return 0;

	uint16_t traced = 1;

	heap_alloc->gc_flag = 1;
	reset_stack[(*reset_count)++] = heap_alloc;
	switch (heap_alloc->trace_mode)
	{
	case GC_TRACE_MODE_ALL:
		for (uint_fast16_t i = 0; i < heap_alloc->limit; i++)
			if (heap_alloc->init_stat[i])
				traced += machine_heap_trace(machine, heap_alloc->registers[i].heap_alloc, reset_stack, reset_count);
		break;
	case GC_TRACE_MODE_SOME:
		for (uint_fast16_t i = 0; i < heap_alloc->limit; i++)
			if(heap_alloc->init_stat[i] && heap_alloc->trace_stat[i])
				traced += machine_heap_trace(machine, heap_alloc->registers[i].heap_alloc, reset_stack, reset_count);
		break;
	}
	return traced;
}

int init_machine(machine_t* machine, uint16_t stack_size, uint16_t heap_alloc_limit, uint16_t frame_limit) {
	machine->heap_alloc_limit = heap_alloc_limit;
	machine->frame_limit = frame_limit;

	machine->last_err = ERROR_NONE;
	machine->global_offset = 0;
	machine->position_count = 0;
	machine->heap_frame = 0;
	machine->heap_count = 0;
	machine->trace_count = 0;
	machine->freed_heap_count = 0;

	ESCAPE_ON_FAIL(machine->stack = malloc(stack_size * sizeof(machine_reg_t)));
	ESCAPE_ON_FAIL(machine->positions = malloc(machine->frame_limit * sizeof(machine_ins_t*)));
	ESCAPE_ON_FAIL(machine->heap_allocs = malloc(machine->heap_alloc_limit * sizeof(heap_alloc_t*)));
	ESCAPE_ON_FAIL(machine->heap_traces = malloc((machine->trace_alloc_limit = 128) * sizeof(heap_alloc_t*)));
	ESCAPE_ON_FAIL(machine->heap_frame_bounds = malloc(machine->frame_limit * sizeof(uint16_t)));
	ESCAPE_ON_FAIL(machine->trace_frame_bounds = malloc(machine->frame_limit * sizeof(uint16_t)));
	ESCAPE_ON_FAIL(machine->freed_heap_allocs = malloc((machine->alloc_freed_heaps = 128) * sizeof(heap_alloc_t*)));
	ESCAPE_ON_FAIL(init_ffi(&machine->ffi_table));
	return 1;
}

void free_machine(machine_t* machine) {
	for (uint_fast16_t i = 0; i < machine->freed_heap_count; i++)
		free(machine->freed_heap_allocs[i]);
	free(machine->freed_heap_allocs);
	free_ffi(&machine->ffi_table);
	free(machine->stack);
	free(machine->positions);
	free(machine->heap_allocs);
	free(machine->heap_frame_bounds);
	free(machine->heap_traces);
	free(machine->trace_frame_bounds);
}

#define AREG ip->a_flag ? ip->a + machine->global_offset : ip->a
#define BREG ip->b_flag ? ip->b + machine->global_offset : ip->b
#define CREG ip->c_flag ? ip->c + machine->global_offset : ip->c

int machine_execute(machine_t* machine, machine_ins_t* instructions) {
	machine_ins_t* ip = instructions;
	for (;;) {
		switch (ip->op_code) {
		case OP_CODE_MOVE:
			machine->stack[AREG] = machine->stack[BREG];
			break;
		case OP_CODE_SET:
			machine->stack[AREG].long_int = ip->b;
			break;
		case OP_CODE_JUMP:
			ip = &instructions[ip->a];
			continue;
		case OP_CODE_JUMP_CHECK:
			if (!machine->stack[AREG].bool_flag) {
				ip = &instructions[ip->b];
				continue;
			}
			break;
		case OP_CODE_CALL: {
			PANIC_ON_FAIL(machine->position_count != machine->frame_limit, machine, ERROR_STACK_OVERFLOW);
			machine->positions[machine->position_count++] = ip;
			uint16_t prev_a = AREG;
			machine->global_offset += ip->b;
			ip = machine->stack[prev_a].ip;
			continue;
		}
		case OP_CODE_LABEL:
			machine->stack[AREG].ip = &instructions[ip->b];
			break;
		case OP_CODE_RETURN: {
			ip = machine->positions[--machine->position_count];
			break;
		}
		case OP_CODE_LOAD_ALLOC: {
			heap_alloc_t* array_register = machine->stack[AREG].heap_alloc;
			int64_t index_register = machine->stack[BREG].long_int;
			if (index_register < 0 || index_register >= array_register->limit)
				PANIC(machine, ERROR_INDEX_OUT_OF_RANGE);
			if (!array_register->init_stat[index_register])
				PANIC(machine, ERROR_READ_UNINIT);
			machine->stack[CREG] = array_register->registers[index_register];
			break;
		}
		case OP_CODE_LOAD_ALLOC_I: {
			heap_alloc_t* array_register = machine->stack[AREG].heap_alloc;
		load_heap_i:
			if (!array_register->init_stat[ip->b])
				PANIC(machine, ERROR_READ_UNINIT);
			machine->stack[CREG] = array_register->registers[ip->b];
			break;
		case OP_CODE_LOAD_ALLOC_I_BOUND:
			array_register = machine->stack[AREG].heap_alloc;
			if (ip->b > array_register->limit)
				PANIC(machine, ERROR_INDEX_OUT_OF_RANGE);
			goto load_heap_i;
		}
		case OP_CODE_STORE_ALLOC: {
			heap_alloc_t* array_register = machine->stack[AREG].heap_alloc;
			int64_t index_register = machine->stack[BREG].long_int;
			if (index_register < 0 || index_register >= array_register->limit)
				PANIC(machine, ERROR_INDEX_OUT_OF_RANGE);
			array_register->registers[index_register] = machine->stack[CREG];
			array_register->init_stat[index_register] = 1;
			break;
		}
		case OP_CODE_STORE_ALLOC_I: {
			heap_alloc_t* array_register = machine->stack[AREG].heap_alloc;
		store_heap_i:
			array_register->registers[ip->b] = machine->stack[CREG];
			array_register->init_stat[ip->b] = 1;
			break;
		case OP_CODE_STORE_ALLOC_I_BOUND:
			array_register = machine->stack[AREG].heap_alloc;
			if (ip->b > array_register->limit)
				PANIC(machine, ERROR_INDEX_OUT_OF_RANGE);
			goto store_heap_i;
		}
		case OP_CODE_DYNAMIC_CONF:
			machine->stack[AREG].heap_alloc->trace_stat[ip->b] = machine->stack[CREG].bool_flag;
			break;
		case OP_CODE_DYNAMIC_CONF_ALL:
			machine->stack[AREG].heap_alloc->trace_mode = machine->stack[BREG].bool_flag;
			break;
		case OP_CODE_CONF_TRACE:
			machine->stack[AREG].heap_alloc->trace_stat[ip->b] = ip->c;
			break;
		case OP_CODE_STACK_OFFSET:
			machine->global_offset += ip->a;
			break;
		case OP_CODE_STACK_DEOFFSET:
			machine->global_offset -= ip->a;
			break;
		case OP_CODE_ALLOC:
			ESCAPE_ON_FAIL(machine->stack[AREG].heap_alloc = machine_alloc(machine, machine->stack[BREG].long_int, ip->c));
			break;
		case OP_CODE_ALLOC_I:
			ESCAPE_ON_FAIL(machine->stack[AREG].heap_alloc = machine_alloc(machine, ip->b, ip->c));
			break;
		case OP_CODE_DYNAMIC_FREE:
			if (!machine->stack[BREG].bool_flag)
				break;
		case OP_CODE_FREE:
			ESCAPE_ON_FAIL(free_alloc(machine, machine->stack[AREG].heap_alloc));
			break;
		case OP_CODE_GC_NEW_FRAME: {
			if (machine->heap_frame == machine->frame_limit)
				PANIC(machine, ERROR_STACK_OVERFLOW);
			machine->heap_frame_bounds[machine->heap_frame] = machine->heap_count;
			machine->trace_frame_bounds[machine->heap_frame] = machine->trace_count;
			machine->heap_frame++;
			break;
		}
		case OP_CODE_DYNAMIC_TRACE: {
			if (!machine->stack[BREG].bool_flag)
				break;
		case OP_CODE_GC_TRACE:
			if (machine->trace_count == machine->trace_alloc_limit) {
				heap_alloc_t** new_trace_stack = realloc(machine->heap_traces, (machine->trace_alloc_limit += 10) * sizeof(heap_alloc_t*));
				PANIC_ON_FAIL(new_trace_stack, machine, ERROR_MEMORY);
				machine->heap_traces = new_trace_stack;
			}
			machine->heap_traces[machine->trace_count++] = machine->stack[AREG].heap_alloc;
			break;
		}
		case OP_CODE_GC_CLEAN: {
			uint16_t reseted_heap_count = 0;
			static heap_alloc_t* reset_stack[64];
			
			--machine->heap_frame;
			if (machine->heap_frame) {
				for (uint_fast16_t i = machine->trace_frame_bounds[machine->heap_frame]; i < machine->trace_count; i++)
					machine_heap_trace(machine, machine->heap_traces[i], reset_stack, &reseted_heap_count);
			}

			heap_alloc_t** frame_start = &machine->heap_allocs[machine->heap_frame_bounds[machine->heap_frame]];
			heap_alloc_t** frame_end = &machine->heap_allocs[machine->heap_count];
			
			for (heap_alloc_t** current_alloc = frame_start; current_alloc != frame_end; current_alloc++) {
				if ((*current_alloc)->gc_flag)
					*frame_start++ = *current_alloc;
				else if ((*current_alloc)->pre_freed)
					(*current_alloc)->reg_with_table = 0;
				else {
					free((*current_alloc)->registers);
					free((*current_alloc)->init_stat);
					if ((*current_alloc)->trace_mode == GC_TRACE_MODE_SOME)
						free((*current_alloc)->trace_stat);
					free((*current_alloc));
				}
			}
			machine->heap_count = frame_start - machine->heap_allocs;
			machine->trace_count = machine->trace_frame_bounds[machine->heap_frame];
			for (uint_fast16_t i = 0; i < reseted_heap_count; i++)
				reset_stack[i]->gc_flag = 0;
			break;
		}
		case OP_CODE_AND:
			machine->stack[CREG].bool_flag = machine->stack[AREG].bool_flag && machine->stack[BREG].bool_flag;
			break;
		case OP_CODE_OR:
			machine->stack[CREG].bool_flag = machine->stack[AREG].bool_flag || machine->stack[BREG].bool_flag;
			break;
		case OP_CODE_NOT:
			machine->stack[AREG].bool_flag = !machine->stack[BREG].bool_flag;
			break;
		case OP_CODE_LENGTH:
			machine->stack[AREG].long_int = machine->stack[BREG].heap_alloc->limit;
			break;
		case OP_CODE_BOOL_EQUAL:
			machine->stack[CREG].bool_flag = machine->stack[AREG].bool_flag == machine->stack[BREG].bool_flag;
			break;
		case OP_CODE_CHAR_EQUAL:
			machine->stack[CREG].bool_flag = machine->stack[AREG].char_int == machine->stack[BREG].char_int;
			break;
		case OP_CODE_LONG_EQUAL:
			machine->stack[CREG].bool_flag = machine->stack[AREG].long_int == machine->stack[BREG].long_int;
			break;
		case OP_CODE_LONG_MORE_EQUAL:
			machine->stack[CREG].bool_flag = machine->stack[AREG].long_int >= machine->stack[BREG].long_int;
			break;
		case OP_CODE_LONG_LESS_EQUAL:
			machine->stack[CREG].bool_flag = machine->stack[AREG].long_int <= machine->stack[BREG].long_int;
			break;
		case OP_CODE_FLOAT_EQUAL:
			machine->stack[CREG].bool_flag = machine->stack[AREG].float_int == machine->stack[BREG].float_int;
			break;
		case OP_CODE_FLOAT_MORE_EQUAL:
			machine->stack[CREG].bool_flag = machine->stack[AREG].float_int >= machine->stack[BREG].float_int;
			break;
		case OP_CODE_FLOAT_LESS_EQUAL:
			machine->stack[CREG].bool_flag = machine->stack[AREG].float_int <= machine->stack[BREG].float_int;
			break;
		case OP_CODE_LONG_MORE:
			machine->stack[CREG].bool_flag = machine->stack[AREG].long_int > machine->stack[BREG].long_int;
			break;
		case OP_CODE_FLOAT_MORE:
			machine->stack[CREG].bool_flag = machine->stack[AREG].float_int > machine->stack[BREG].float_int;
			break;
		case OP_CODE_LONG_LESS:
			machine->stack[CREG].bool_flag = machine->stack[AREG].long_int < machine->stack[BREG].long_int;
			break;
		case OP_CODE_FLOAT_LESS:
			machine->stack[CREG].bool_flag = machine->stack[AREG].float_int < machine->stack[BREG].float_int;
			break;
		case OP_CODE_LONG_ADD:
			machine->stack[CREG].long_int = machine->stack[AREG].long_int + machine->stack[BREG].long_int;
			break;
		case OP_CODE_LONG_SUBRACT:
			machine->stack[CREG].long_int = machine->stack[AREG].long_int - machine->stack[BREG].long_int;
			break;
		case OP_CODE_LONG_MULTIPLY:
			machine->stack[CREG].long_int = machine->stack[AREG].long_int * machine->stack[BREG].long_int;
			break;
		case OP_CODE_LONG_DIVIDE: {
			uint64_t d = machine->stack[BREG].long_int;
			PANIC_ON_FAIL(d, machine, ERROR_DIVIDE_BY_ZERO);
			machine->stack[CREG].long_int = machine->stack[AREG].long_int / d;
			break; 
		}
		case OP_CODE_LONG_MODULO:
			machine->stack[CREG].long_int = machine->stack[AREG].long_int % machine->stack[BREG].long_int;
			break;
		case OP_CODE_LONG_EXPONENTIATE:
			machine->stack[CREG].long_int = longpow(machine->stack[AREG].long_int, machine->stack[BREG].long_int);
			break;
		case OP_CODE_FLOAT_ADD:
			machine->stack[CREG].float_int = machine->stack[AREG].float_int + machine->stack[BREG].float_int;
			break;
		case OP_CODE_FLOAT_SUBTRACT:
			machine->stack[CREG].float_int = machine->stack[AREG].float_int - machine->stack[BREG].float_int;
			break;
		case OP_CODE_FLOAT_MULTIPLY:
			machine->stack[CREG].float_int = machine->stack[AREG].float_int * machine->stack[BREG].float_int;
			break;
		case OP_CODE_FLOAT_DIVIDE: {
			float d = machine->stack[BREG].float_int;
			PANIC_ON_FAIL(d, machine, ERROR_DIVIDE_BY_ZERO);
			machine->stack[CREG].float_int = machine->stack[AREG].float_int / d;
			break; 
		}
		case OP_CODE_FLOAT_MODULO:
			machine->stack[CREG].float_int = fmod(machine->stack[AREG].float_int, machine->stack[BREG].float_int);
			break;
		case OP_CODE_FLOAT_EXPONENTIATE:
			machine->stack[CREG].float_int = pow(machine->stack[AREG].float_int, machine->stack[BREG].float_int);
			break;
		case OP_CODE_LONG_NEGATE:
			machine->stack[AREG].long_int = -machine->stack[BREG].long_int;
			break;
		case OP_CODE_FLOAT_NEGATE:
			machine->stack[AREG].float_int = -machine->stack[BREG].float_int;
			break;
		case OP_CODE_ABORT:
			if (ip->a == ERROR_NONE)
				return 1;
			else
				PANIC(machine, ip->a);
		case OP_CODE_FOREIGN:
			if (!ffi_invoke(&machine->ffi_table, machine, &machine->stack[AREG], &machine->stack[BREG], &machine->stack[CREG]))
				if (machine->last_err == ERROR_NONE)
					machine->last_err = ERROR_FOREIGN;
				else
					return 0;
			break;
		}
		ip++;
	}
	return 1;
}