#pragma once

#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>

/// internally-used indexing type
typedef uint32_t json_index_t;

/// JSON numeric value representation in C
typedef double json_number_t;

/// JSON value type enum
typedef enum {
	JSON_TYPE_NULL = 0,
	
	// primitives
	JSON_TYPE_NUMBER,
	JSON_TYPE_BOOLEAN,
	JSON_TYPE_STRING,
	
	// containers
	JSON_TYPE_ARRAY,
	JSON_TYPE_OBJECT
} json_type_t;

/// JSON value object
typedef struct json_value_s* json_value_ref;

/// JSON parsing error structure
typedef struct {
	// if this is true, then all else is valid
	bool fail;
	
	// error location
	json_index_t line;
	json_index_t character;
	
	// error message
	char* message;
} json_error;

json_value_ref json_parse(const char* input, json_error* errorP);

void json_value_dump_tree(json_value_ref value, const json_index_t offset);

void json_value_release_tree(json_value_ref value);
void json_value_release(json_value_ref value);