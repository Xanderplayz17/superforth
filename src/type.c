#include <stdlib.h>
#include "error.h"
#include "type.h"

const int init_typecheck_type(typecheck_type_t* typecheck_type, const int has_sub_types) {
	if (has_sub_types)
		ESCAPE_ON_NULL(typecheck_type->sub_types = malloc(TYPE_MAX_SUBTYPES * sizeof(typecheck_type_t)))
	else
		typecheck_type->sub_types = NULL;
	typecheck_type->sub_type_count = 0;
	return 1;
}

void free_typecheck_type(typecheck_type_t* typecheck_type) {
	for (uint_fast8_t i = 0; i < typecheck_type->sub_type_count; i++)
		free_typecheck_type(&typecheck_type->sub_types[i]);
	if (typecheck_type->sub_types)
		free(typecheck_type->sub_types);
}

const int copy_typecheck_type(typecheck_type_t* dest, typecheck_type_t src) {
	dest->type = src.type;
	dest->sub_type_count = src.sub_type_count;
	dest->id = src.id;
	if (src.sub_type_count) {
		ESCAPE_ON_NULL(dest->sub_types = malloc(src.sub_type_count * sizeof(typecheck_type_t)));
		for (uint_fast8_t i = 0; i < src.sub_type_count; i++)
			copy_typecheck_type(&dest->sub_types[i], src.sub_types[i]);
	}
	else
		dest->sub_types = NULL;
	return 1;
}

const int type_decl_sub_type(typecheck_type_t* super_type, typecheck_type_t sub_type) {
	ESCAPE_ON_NULL(super_type->sub_types);
	if (super_type->sub_type_count == TYPE_MAX_SUBTYPES)
		return 0;
	super_type->sub_types[super_type->sub_type_count++] = sub_type;
	return 1;
}

const int typecheck_type_compatible(typecheck_type_t target_type, typecheck_type_t match_type) {
	if (target_type.type == TYPE_GENERIC_PARAM && match_type.type == TYPE_GENERIC_PARAM)
		return target_type.id == match_type.id;
	if (target_type.type < TYPE_SUPER_ARRAY)
		return target_type.type == match_type.type;
	else {
		if (target_type.type != match_type.type || target_type.sub_type_count != match_type.sub_type_count)
			return 0;
		for (uint_fast8_t i = 0; i < target_type.sub_type_count; i++)
			if (target_type.sub_types[i].type != match_type.sub_types[i].type)
				return 0;
		return 1;
	}
}

static void typecheck_finalize_subtype(typecheck_type_t* sub_type, uint64_t* hash_ids, uint8_t* limit) {
	if (sub_type->type == TYPE_GENERIC_PARAM) {
		for (int_fast16_t i = *limit - 1; i >= 0; i--)
			if (hash_ids[i] == sub_type->id) {
				sub_type->id = i;
				return;
			}
		hash_ids[*limit] = sub_type->id;
		sub_type->id = (*limit)++;
	}
	else if (sub_type->type >= TYPE_SUPER_ARRAY) {
		for (uint_fast8_t i = 0; i < sub_type->sub_type_count; i++)
			typecheck_finalize_subtype(&sub_type->sub_types[i], hash_ids, limit);
	}
}

uint64_t typecheck_finalize_generics(typecheck_type_t* type) {
	uint64_t hash_ids[TYPE_MAX_SUBTYPES];
	uint64_t hash_count = 0;
	typecheck_finalize_subtype(type, &hash_ids, &hash_count);
	return hash_count;
}

const int typecheck_match_typearg(typecheck_type_t* param_type, typecheck_type_t arg_type, typecheck_type_t* matched_type_args, int* inputted_type_args) {
	if (param_type->type == TYPE_GENERIC_PARAM) {
		if (!inputted_type_args[param_type->id]) {
			matched_type_args[param_type->id] = arg_type;
			inputted_type_args[param_type->id] = 1;
			return copy_typecheck_type(param_type, arg_type);
		}
		return typecheck_type_compatible(matched_type_args[param_type->id], arg_type);
	}
	else {
		if (param_type->type != arg_type.type)
			return 0;
		if (param_type->type >= TYPE_SUPER_ARRAY) {
			if (param_type->sub_type_count != arg_type.sub_type_count)
				return 0;
			for (uint_fast8_t i = 0; i < param_type->sub_type_count; i++)
				ESCAPE_ON_NULL(typecheck_match_typearg(&param_type->sub_types[i], arg_type.sub_types[i], matched_type_args, inputted_type_args));
		}
	}
	return 1;
}

const int typecheck_has_generics(typecheck_type_t type) {
	if (type.type == TYPE_GENERIC_PARAM)
		return 1;
	else if (type.type >= TYPE_SUPER_ARRAY) {
		for (uint_fast8_t i = 0; i < type.sub_type_count; i++)
			if (typecheck_has_generics(type.sub_types[i]))
				return 1;
	}
	return 0;
}