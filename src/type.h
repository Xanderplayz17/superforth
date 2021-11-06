#pragma once

#ifndef TYPE_H
#define TYPE_H

#include <stdint.h>

#define TYPE_MAX_SUBTYPES 100

typedef struct typecheck_type typecheck_type_t;

typedef enum typecheck_base_type {
	TYPE_AUTO,
	TYPE_NOTHING,
	TYPE_TYPEARG,

	TYPE_PRIMATIVE_BOOL,
	TYPE_PRIMATIVE_CHAR,
	TYPE_PRIMATIVE_LONG,
	TYPE_PRIMATIVE_FLOAT,

	TYPE_SUPER_ARRAY,
	TYPE_SUPER_PROC,
} typecheck_base_type_t;

typedef struct typecheck_type {
	typecheck_base_type_t type;
	typecheck_type_t* sub_types;
	uint8_t sub_type_count, match;
} typecheck_type_t;

static typecheck_type_t typecheck_int = { .type = TYPE_PRIMATIVE_LONG };
static typecheck_type_t typecheck_float = { .type = TYPE_PRIMATIVE_FLOAT };
static typecheck_type_t typecheck_char = { .type = TYPE_PRIMATIVE_CHAR };
static typecheck_type_t typecheck_bool = { .type = TYPE_PRIMATIVE_BOOL };
static typecheck_type_t typecheck_array = { .type = TYPE_SUPER_ARRAY };

void free_typecheck_type(typecheck_type_t* typecheck_type);
int copy_typecheck_type(typecheck_type_t* dest, typecheck_type_t src);

int typecheck_compatible(typecheck_type_t* target_type, typecheck_type_t match_type);

int typecheck_has_type(typecheck_type_t type, typecheck_base_type_t base_type);

int type_args_substitute(typecheck_type_t* input_type_args, typecheck_type_t* proto_type);

#endif // !TYPE