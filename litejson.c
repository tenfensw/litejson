#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "litejson.h"

//
// private
//

#define LJ_ERROR_CHARMAX 512

#define LJ_STRINGOPS_BUFSTEP 20
#define LJ_STRINGOPS_JSONTOK '\r'

#ifdef LJ_DEBUG_ALLOW_COLORS
#define LJ_PRINTF_ADDRESS "\033[93m"
#define LJ_PRINTF_GREEN "\033[92m"
#define LJ_PRINTF_DEBUG "\033[96m"
#define LJ_PRINTF_RESET "\033[0m"
#else
#define LJ_PRINTF_ADDRESS ""
#define LJ_PRINTF_GREEN ""
#define LJ_PRINTF_DEBUG ""
#define LJ_PRINTF_RESET ""
#endif

struct json_value_s {
	// value parent container, NULL if this is the root 
	// one
	json_value_ref parent;
	
	// stored value type
	json_type_t type;
	
	// key/label if stored in an object
	char* key;
	
	// string value
	char* strV;
	// numeric value
	json_number_t numV;
	
	// first child item (container-only)
	json_value_ref child;
	// next item
	json_value_ref next;
};

void ljprintf(const char* fn, const json_index_t line, const char* fc,
			  const char* msgF, ...) {
#ifdef DEBUG
	va_list vl;
	va_start(vl, msgF);

	fprintf(stderr, "%s[%s:%u %s]%s ", LJ_PRINTF_DEBUG,
			fn, line, fc, LJ_PRINTF_RESET);
	vfprintf(stderr, msgF, vl);
	fprintf(stderr, "%c", '\n');
	
	va_end(vl);
#else
	(void)(fn);
	(void)(line);
	(void)(fc);
	(void)(msgF);
#endif
}

#define ljprintf(...) ljprintf(__FILE__, __LINE__, __FUNCTION__, \
							   __VA_ARGS__)

/// zero malloc
void* ljmalloc(const json_index_t size) {
	if (size < 1)
		return NULL; // obv
		
	void* result = malloc(size);
	bzero(result, size);
	
	return result;
}

#define ljmalloc_s(nm) ljmalloc(sizeof(struct nm))

json_error json_error_make(const json_index_t line,
						   const json_index_t character,
						   const char* msgF,
						   ...) {
	json_error result = { (msgF ? true : false), line, character, NULL };
	
	if (msgF) {
		// merge the specified error message template with its parameters
		// into a single string
		
		char* msg = calloc(LJ_ERROR_CHARMAX, sizeof(char)); 
		
		va_list vl;
		va_start(vl, msgF);
		
		vsprintf(msg, msgF, vl);
		
		va_end(vl);
		
		result.message = msg;
	}
	
	return result;
}

#define LJ_IF_NOT_NULL(p1, p2) \
{ \
	if (p1) \
		(*p1) = p2; \
}

#define LJ_ERROR(...) \
{ \
	if (value) \
		json_value_release_tree(value); \
	LJ_IF_NOT_NULL(errorP, json_error_make(lineC, charC, __VA_ARGS__)) \
	return NULL; \
}

#define LJ_IS_CONTAINER(obj) (obj->type == JSON_TYPE_ARRAY || \
							  obj->type == JSON_TYPE_OBJECT)

#define LJ_IS_JSONTOK(current) \
	(isspace(current) != 0 || current == ']' || current == '}' || current == ',')
			
#define LJ_INIT_EMPTY_OBJ(newObj) \
json_value_ref newObj = ljmalloc_s(json_value_s); \
\
if (futureKey) { \
	newObj->key = futureKey; \
	futureKey = NULL; \
} \
\
if (!root) \
	root = newObj;
							  
#define LJ_ADAPT_OBJ_PARENT(newObj) \
{ \
	if (value) { \
		if (LJ_IS_CONTAINER(value)) { \
			newObj->parent = value; \
			if (value->child) { \
				json_value_ref lastChild = json_value_get_last(value); \
				lastChild->next = newObj; \
			} else \
				value->child = newObj; \
		} else { \
			value->next = newObj; \
			newObj->parent = value->parent; \
		} \
	} \
}

/// internally-used parsing state machine
typedef enum {
	JSON_STATE_OUTSIDE = 0,
	
	JSON_STATE_KEY = 90,
	JSON_STATE_VALUE = 91
} json_parse_state_t;

json_index_t lj_substring_until(const char* input, const char delim1,
								const char delim2, char** resultP,
								const bool respectQuotes) {
	if (!input || strlen(input) < 1 || delim1 == '\0') {
		// need to have at least one delimiter specified
		ljprintf("input = \"%s\", while delim1 = %d, cannot continue", 
				 input, (int8_t)delim1);
		return 0;
	} else if (input[0] == delim1 || input[0] == delim2)
		input++; // chop off right to the start
	
	// automatically extendable buffer
	json_index_t resultSize = LJ_STRINGOPS_BUFSTEP;
	json_index_t resultLength = 0;
	char* result = calloc(resultSize, sizeof(char));
	
	// if true, then we were preceded by a '\\'
	bool insideEscape = false;
	// if true, we are in double quotes (ignored without respectQuotes)
	bool insideQuotes = false;
		
	for (json_index_t index = 0; index < strlen(input); index++) {
		char current = input[index];
		
		// extend the buffer first if necessary
		if ((resultLength + 2) >= resultSize) {
			ljprintf("buffer limit reached (size = %u), result = \"%s\"",
					 resultSize, result);
					 
			resultSize += LJ_STRINGOPS_BUFSTEP;
			result = realloc(result, sizeof(char) * resultSize);
		}
	
		if (insideEscape) {
			// escape the character requested
			switch (current) {
				case 't': {
					current = '\t';
					break;
				}
				case 'n': {
					current = '\n';
					break;
				}
				case 'r': {
					current = '\r';
					break;
				}
				default:
					break;
			}
		} else if (current == '\\') {
			insideEscape = true;
			continue;
		} else if (current == '"' && respectQuotes)
			insideQuotes = !insideQuotes;
		
		if (!insideQuotes) {
			if (current == delim1 || current == delim2)
				break; // found 'em
			else if (delim1 == LJ_STRINGOPS_JSONTOK && 
					 LJ_IS_JSONTOK(current))
				break;
		}
		
		result[resultLength++] = current;
		result[resultLength] = '\0';
			
		insideEscape = false;
	}
	
	if (resultP)
		(*resultP) = result;
	else
		free(result); // avoid memory leak
		
	return resultLength;
}

void lj_substring_strip_right(char* input) {
	if (!input)
		return;
	
	json_index_t length = strlen(input);
	if (length < 1)
		return;
		
	while (length >= 1) {
		length--;
		
		if (isspace(input[length]) == 0)
			break; // end space search
			
		input[length] = '\0';
	}
}

/// verifies if the specified C string contains a stringified number
bool ljisdigit_str(const char* input) {
	if (!input)
		return false; // nothing to look for
		
	for (json_index_t i = 0; i < strlen(input); i++) {
		if (isdigit(input[i]) == 0 && input[i] != '-' && input[i] != '+' && input[i] != '.')
			return false;
	}
	
	return true;
}

/// C string -> double
json_number_t ljatof(const char* input) {
	if (!ljisdigit_str(input))
		return 0.0;
	
	json_number_t result = 0.0; 
	sscanf(input, "%lf", &result);
	
	return result;
}

/// checks if the specified object value stores the specified item in itself
json_value_ref json_value_get_last(json_value_ref first) {
	if (!first) {
		ljprintf("NULL specified, stopping");
		return NULL;
	}
	
	json_value_ref result = first;
	
	if (LJ_IS_CONTAINER(first))
		result = first->child; // use first child instead
		
	while (result->next)
		result = result->next;
		
	return result;
}

//
// public
//

void json_value_release_tree(json_value_ref value) {
	// TODO
	(void)(value);
}

void json_value_release(json_value_ref value) {
	// TODO
	(void)(value);
}

void json_value_dump_tree(json_value_ref value, const json_index_t offset) {
	if (!value)
		fprintf(stderr, "(null value)\n");
	else {
		// type out the correct amount of spaces
		for (json_index_t i = 0; i < offset; i++)
			fprintf(stderr, "%c", ' ');


		fprintf(stderr, "%s%p%s ", LJ_PRINTF_ADDRESS, value, LJ_PRINTF_RESET);
	
		if (value->key)
			fprintf(stderr, "key = \"%s\", ", value->key);
		else
			fprintf(stderr, "no key, ");
			
		fprintf(stderr, "type = %u, container = %s, strV = \"%s\", numV = %f, parent = %p\n", 
				value->type, LJ_IS_CONTAINER(value) ? "true" : "false", value->strV, value->numV,
				value->parent);
		
		if (value->child)
			json_value_dump_tree(value->child, offset + 1);
		
		if (value->next)
			json_value_dump_tree(value->next, offset);
	}
}

json_value_ref json_parse(const char* input, json_error* errorP) {
	if (!input || strlen(input) < 1) {
		// don't bother parsing empty strings
		
		LJ_IF_NOT_NULL(errorP, json_error_make(0, 0, "NULL or empty string provided as input"));
		return NULL;
	}
	
	// success boilerplate saved for the future
	LJ_IF_NOT_NULL(errorP, json_error_make(0, 0, NULL))
	
	// for detailed parsing error reporting
	json_index_t lineC = 1;
	json_index_t charC = 0;
	
	// state machine
	json_parse_state_t state = JSON_STATE_OUTSIDE;
	// current value object
	json_value_ref value = NULL;
	
	// root container object
	json_value_ref root = NULL;
	
	// key that might be set for the next found value
	char* futureKey = NULL;
	
	for (json_index_t index = 0; index < strlen(input); index++) {
		const char current = input[index];
		charC++;
	
		// skip all spaces as we aren't interested in them
		if (isspace(current) != 0) {
			if (current == '\n') {
				// update line/character counters accordingly
				
				lineC++;
				charC = 0;
			}
		
			continue;
		}
		
		ljprintf("current = '%c', index = %u", current, index);
		
		if (current == '{' && state != JSON_STATE_KEY) {
			// looks like we are going deeper and are starting an object
			ljprintf("handled: new object {} (state = %u)", state);
			
			LJ_INIT_EMPTY_OBJ(newObj)
			
			newObj->type = JSON_TYPE_OBJECT;
			newObj->parent = value;
			
			// adapt parent attributes correctly
			LJ_ADAPT_OBJ_PARENT(newObj)
			value = newObj;
			
			ljprintf("state change -> key (new object)");
			state = JSON_STATE_KEY;
			
			ljprintf("%s enter new object from %p to %p %s", 
					 LJ_PRINTF_GREEN, value->parent, value, LJ_PRINTF_RESET);
			
			continue;
		} else if (state == JSON_STATE_OUTSIDE) {
			ljprintf("handled: outside");
		
			if (current == '"') {
				// string, probably
				
				char* strV = NULL;
				json_index_t strVLength = lj_substring_until(input + index, '"', '\0', &strV, false);
				
				index += strVLength + 1;
				
				// make an empty string if it is one
				if (!strV)
					strV = ljmalloc(sizeof(char));
					
				ljprintf("found a string, strV = \"%s\" (length = %u)", strV, strVLength);
				
				LJ_INIT_EMPTY_OBJ(newObj)
			
				newObj->type = JSON_TYPE_STRING;
				newObj->strV = strV;
				newObj->numV = ljatof(strV);
			
				// adapt parent attributes correctly
				LJ_ADAPT_OBJ_PARENT(newObj)
				value = newObj;
			} else {
				// TODO: support arrays
			
				// need to read till the end first
				
				char* token = NULL;
				json_index_t tokenLength = lj_substring_until(input + index, LJ_STRINGOPS_JSONTOK, '\0', &token, true);
				
				index += tokenLength + 1;
				
				ljprintf("found a (yet) undefined token = \"%s\", length = %u", token, tokenLength);
				
				LJ_INIT_EMPTY_OBJ(newObj)
				
				if (strcmp(token, "null") == 0) {
					// a null value, add a placeholder string value and
					// move on
					newObj->strV = ljmalloc(sizeof(char));
					free(token);
				} else if (ljisdigit_str(token)) {
					// a numeric value
					newObj->type = JSON_TYPE_NUMBER;
					newObj->strV = token;
					newObj->numV = ljatof(token);
				} else if (strcmp(token, "true") == 0 || 
						   strcmp(token, "false") == 0) {
					// a boolean value
					newObj->type = JSON_TYPE_BOOLEAN;
					newObj->strV = token;
					newObj->numV = (strcmp(token, "true") == 0);
				} else {
					free(token); // quick clean up
				
					json_value_release(newObj);
					LJ_ERROR("Expected a valid JSON value, got '%c' token", current);
				} 
				
				// adapt parent attributes correctly
				LJ_ADAPT_OBJ_PARENT(newObj)
				value = newObj;
				continue;
			}
		}
		
		if (state == JSON_STATE_KEY) {
			ljprintf("handled: object state -> key");
		
			// we are inside {} (object) - looking for keys
			if (current == '"') {
				ljprintf("key found at %u", index);
			
				char* key = NULL;
				json_index_t keyLength = lj_substring_until(input + index, '"', '\0', &key, false);
				
				index += keyLength + 1;
				futureKey = key;
				
				ljprintf("futureKey = \"%s\", length = %u", futureKey, keyLength);
				continue;
			} else if (current == ':') {
				ljprintf("key delimiter found at %u", index);
				
				// need the key to be found first
				if (!futureKey)
					LJ_ERROR("Expected key, got '%c' instead", current)
					
				ljprintf("state change -> value");
				state = JSON_STATE_VALUE;
			} else if (current == ',') {
				ljprintf("comma jump");
				continue;
			} else if (current == '}') {
				// the end of this object
				ljprintf("reached } at %u", current);
				
				if (value && value->type != JSON_TYPE_OBJECT &&
					value->parent && value->parent->type == JSON_TYPE_OBJECT) {
					// we are a child object, so we need to go up to our parent
					
					ljprintf("additional level up from %p to %p, type %u -> {}",
							 value, value->parent, value->type);
					value = value->parent;
				}
				
				if (!value)
					LJ_ERROR("Stray object end token")
				
				if (value->parent) {
					ljprintf("%s going up one level from %p to %p %s", 
							 LJ_PRINTF_GREEN, value, value->parent, LJ_PRINTF_RESET);
							 
					// go up
					value = value->parent;
					
					if (value->type != JSON_TYPE_OBJECT)
						state = JSON_STATE_OUTSIDE;
					else
						state = JSON_STATE_KEY;
				} else {
					ljprintf("de facto end of file reached, saying goodbyes and finishing up");
					break;
				}
					
				ljprintf("state change -> %u", state);
			} else
				LJ_ERROR("Expected key, got '%c' instead", current);
		} else if (state == JSON_STATE_VALUE) {
			// found value, need to get its raw string and then recursively parse
			// it
			ljprintf("handled: object state -> value");
			
			char* valueRaw = NULL;
			json_index_t valueRawLength = lj_substring_until(input + index, ',', '}', &valueRaw, false);
			
			if (!valueRaw)
				LJ_ERROR("Expected value, got nothing")
			
			ljprintf("found valueRaw = \"%s\" (%u) at %u", valueRaw, valueRawLength, index);
			index += valueRawLength - 1;
			
			ljprintf("index updated to %u -> '%c'", index, input[index]);
			
			// strip spaces from the ending
			lj_substring_strip_right(valueRaw);
			
			// time of recursive parsing
			json_error adaptedError;
			json_value_ref newObj = json_parse(valueRaw, &adaptedError);
			
			// avoid memory leaks and clean up a bit
			free(valueRaw);
			
			if (adaptedError.fail) {
				// first adapt error report values to proper ones
				adaptedError.line += lineC - 1;
				adaptedError.character += charC;
				
				// now we have to fail the parent too
				json_value_release_tree(value);
				LJ_IF_NOT_NULL(errorP, adaptedError)
				return NULL;
			}
			
			// otherwise, we got ourselves a new object
			ljprintf("new object <%p>, key = \"%s\", type = %u", newObj, futureKey, newObj->type);
			
			if (futureKey) {
				// set the object's label appropriately
				newObj->key = futureKey;
				futureKey = NULL;
			}
			
			// adapt object parent
			LJ_ADAPT_OBJ_PARENT(newObj)
			value = newObj;
			
			if (value->parent && value->parent->type == JSON_TYPE_OBJECT) {
				ljprintf("state changed -> key");
				state = JSON_STATE_KEY;
			}
		}
	}
	
	return root;
}