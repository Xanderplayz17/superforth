#include <stdlib.h>
#include <stdio.h>
#include "compiler.h"
#include "debug.h"

int main() {

	machine_t machine;
	compiler_t* compiler = malloc(sizeof(compiler_t));
	init_compiler(compiler, "auto index_at = generic elemType(array<generic elemType> elements, int index) { return elements[index]; }; array<int> myArray = [1, 2, 3, 4]; auto elem = index_at(myArray, 2);");

	machine_ins_t* instructions;
	uint64_t instruction_count;
	compile(compiler, &machine, &instructions, &instruction_count);
	
	print_instructions(instructions, instruction_count);

	machine_execute(&machine, instructions, instruction_count);

	free(instructions);
	free_machine(&machine);
	free_compiler(compiler);
	free(compiler);
	return 0;
}