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

///
/// parses the specified C string containing a valid JSON document and returns
/// the root container containing all the other values. On error, *errorP is 
/// set to an instance of json_error explaining what went wrong and NULL is 
/// returned
///
json_value_ref json_parse(const char* input, json_error* errorP);

/// creates a new JSON string value with the specified contents (can't be NULL)
json_value_ref json_value_init_string(const char* str);
/// creates a new JSON numeric value with the specified number
json_value_ref json_value_init_number(const json_number_t num);
/// creates a new JSON boolean value
json_value_ref json_value_init_boolean(const bool bv);

/// creates a JSON null instance
json_value_ref json_value_init_null(void);

/// creates an empty JSON object
json_value_ref json_value_init_object(void);
/// creates an empty JSON array
json_value_ref json_value_init_array(void);

/// sets the specified value to a string
bool json_value_set_string(json_value_ref value, const char* str);
/// sets the specified value to a number
bool json_value_set_number(json_value_ref value, const json_number_t num);
/// sets the specified value to a boolean
bool json_value_set_boolean(json_value_ref value, const bool bv);

///
/// retreives the string representation of the specified JSON value, if
/// available
///
const char* json_value_get_string(const json_value_ref value);
///
/// retreives the numeric representation of the specified JSON value, if
/// available
///
json_number_t json_value_get_number(const json_value_ref value);
///
/// retreives the boolean representation of the specified JSON value, if
/// available
///
bool json_value_get_boolean(const json_value_ref value);

///
/// retreives the key name associated with the specified value if it is
/// stored in an object
///
const char* json_value_get_key(const json_value_ref value);
/// retreives stored value type
json_type_t json_value_get_type(const json_value_ref value);

///
/// retreives value stored in the object with the specified key name. If 
/// no value with the specified key is found, NULL is returned
///
json_value_ref json_value_get(const json_value_ref container,
							  const char* key);
///
/// replaces or stores the specified value in the object with the specified 
/// key name. Keep in mind that the value instance pointed by the last argument
/// is NOT copied and MUST NOT BE deallocated manually
///
bool json_value_set(const json_value_ref container, const char* key,
					json_value_ref value);

json_value_ref json_value_get_first(const json_value_ref container);
json_value_ref json_value_get_last(const json_value_ref container);
json_value_ref json_value_get_at(const json_value_ref container,
								 const json_index_t where);

bool json_value_push(json_value_ref container, json_value_ref value);

bool json_value_remove_first(json_value_ref container);
bool json_value_remove_last(json_value_ref container);
bool json_value_remove_at(json_value_ref container,
						  const json_index_t where);
	
///
/// retreives stored child values' count in the specified value representing
/// a container (array, object)			
///	
json_index_t json_value_get_count(const json_value_ref container);

/// stringifies the specified JSON value into a valid JSON document
char* json_value_stringify(const json_value_ref container,
						   const bool humanReadable);

/// releases the specified JSON value object and all of its affiliate values
void json_value_release_tree(json_value_ref value);
/// releases just the specified JSON value object
void json_value_release(json_value_ref value);