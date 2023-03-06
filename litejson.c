#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "litejson.h"

//
// common - private
//

/// max length of json_error.message field
#define LJ_ERROR_CHARMAX 512

/// internally-used base size for automatically extendable C 
/// string buffers
#define LJ_STRINGOPS_BUFSTEP 20
/// max length of ljftoa C string buffer
#define LJ_STRINGOPS_NUMMAX 10
/// const parameter that is used as a "sign" to lj_substring_until to
/// use a common set of JSON token delimiters as its border
#define LJ_STRINGOPS_JSONTOK '\r'
/// JSON document generation soft tab size
#define LJ_STRINGOPS_TABSIZE 3

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

///
/// internally-used debug printf function - release builds do not produce
/// any debug output
///
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

/// convenience wrapper for zero malloc-ing new struct instances
#define ljmalloc_s(nm) ljmalloc(sizeof(struct nm))

//
// json_parse - private
//

///
/// creates a new instance of the json_error structure with the provided
/// data being saved
///
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

/// sets *p1 to p2 if p1 is a valid pointer
#define LJ_IF_NOT_NULL(p1, p2) \
{ \
	if (p1) \
		(*p1) = p2; \
}

/// cleans up json_parse-related vars and throws a parsing error
#define LJ_ERROR(...) \
{ \
	if (value) \
		json_value_release_tree(value); \
	LJ_IF_NOT_NULL(errorP, json_error_make(lineC, charC, __VA_ARGS__)) \
	return NULL; \
}

#define LJ_IS_CONTAINER(obj) (obj->type == JSON_TYPE_ARRAY || \
							  obj->type == JSON_TYPE_OBJECT)

/// checks if the specified character is a JSON delimiter token
#define LJ_IS_JSONTOK(current) \
	(isspace(current) != 0 || current == ']' || current == '}' || current == ',')
			
/// [json_parse only] convenience macro to setup a new empty json_value_ref
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

///
/// [json_parse] if we are a child item inside a container and we encounter
/// a token meant to close that container, we need to jump to our parent
/// container first and only then continue with closing it (jumping to its
/// parent)
///
#define LJ_JUMP_TO_CHILDS_PARENT_MULTI(value, pType) \
{ \
	if (value && value->type != pType && \
		value->parent && value->parent->type == pType) { \
		\
		ljprintf("additional level up from %p to %p, type %u -> %u", \
				 value, value->parent, value->type, pType); \
		value = value->parent; \
	} \
}

///
/// [json_parse] adjust the state machine after jumping to the container's
/// parent
///
#define LJ_CLOSE_AND_JUMP_TO_PARENT(value) \
{ \
	if (value->parent) { \
		ljprintf("%s going up one level from %p to %p %s", \
				 LJ_PRINTF_GREEN, value, value->parent, LJ_PRINTF_RESET); \
		\
		value = value->parent; \
		\
		if (value->type == JSON_TYPE_OBJECT) \
			state = JSON_STATE_KEY; \
		else if (value->type == JSON_TYPE_ARRAY) \
			state = JSON_STATE_ARRAY; \
		else \
			state = JSON_STATE_KEY; \
	} else { \
		ljprintf("de facto end of file reached, saying goodbyes and finishing up"); \
		break; \
	} \
}

/// internally-used parsing state machine
typedef enum {
	JSON_STATE_OUTSIDE = 0,
	
	JSON_STATE_KEY = 90,
	JSON_STATE_VALUE = 91,
	
	JSON_STATE_ARRAY = 80
} json_parse_state_t;

///
/// makes a substring at *resultP and returns its length. delim1 and delim2
/// are parameters signifying the stop characters to look for while going through
/// the main string. If respectQuotes is specified, then those stop characters
/// are ignored when wrapped around in doublequotes
///
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

///
/// strips all isspace() conforming characters from the specified C string's
/// ending
///
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

//
// json_parse - public
//

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
		
		if (state != JSON_STATE_KEY) {
			if (current == '{') {
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
			} else if (current == '[') {
				ljprintf("handled: new array [] (state = %u)", state);
				
				LJ_INIT_EMPTY_OBJ(newObj)
				
				newObj->type = JSON_TYPE_ARRAY;
				
				// adapt parent attributes correctly
				LJ_ADAPT_OBJ_PARENT(newObj)
				value = newObj;
				
				ljprintf("state change -> array (new array)");
				state = JSON_STATE_ARRAY;
				
				ljprintf("%s enter new array from %p to %p %s", 
						 LJ_PRINTF_GREEN, value->parent, value, LJ_PRINTF_RESET);
				continue;
			}
		}
		
		if (state == JSON_STATE_OUTSIDE) {
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
				
				// if we are a child item, we need to first reach out parent
				// container to close it
				LJ_JUMP_TO_CHILDS_PARENT_MULTI(value, JSON_TYPE_OBJECT)
				
				if (!value)
					LJ_ERROR("Stray object end token")
				
				LJ_CLOSE_AND_JUMP_TO_PARENT(value)
					
				ljprintf("state change -> %u", state);
			} else
				LJ_ERROR("Expected key, got '%c' instead", current);
		} else if (state == JSON_STATE_VALUE || state == JSON_STATE_ARRAY) {
			// found value, need to get its raw string and then recursively parse
			// it
			ljprintf("handled: object state -> value");
					 
			const char ending = (state == JSON_STATE_ARRAY) ? ']' : '}';
			ljprintf("recognized ending - '%c'", ending);
			
			// handle array ending correctly
			if (state == JSON_STATE_ARRAY) {
				if (current == ',') {
					// array comma jump
					ljprintf("array comma jump");
					continue;
				} else if (current == ']') {
					ljprintf("reached the closing bracket ] at %u", index);
					
					LJ_JUMP_TO_CHILDS_PARENT_MULTI(value, JSON_TYPE_ARRAY)
				
					if (!value)
						LJ_ERROR("Stray array end token")
				
					LJ_CLOSE_AND_JUMP_TO_PARENT(value)
					
					ljprintf("state change -> %u", state);
					continue;
				}
			}
			
			char* valueRaw = NULL;
			json_index_t valueRawLength = lj_substring_until(input + index, ',', ending, &valueRaw, false);
			
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

// undef all json_parse-related macros so that they won't be used in
// other methods by accident
#undef LJ_CLOSE_AND_JUMP_TO_PARENT
#undef LJ_JUMP_TO_CHILDS_PARENT_MULTI
#undef LJ_ADAPT_OBJ_PARENT
#undef LJ_INIT_EMPTY_OBJ
#undef LJ_ERROR

//
// json_value_ref API - private
//

json_value_ref json_value_init(const json_type_t type) {
	json_value_ref result = ljmalloc_s(json_value_s);
	result->type = type;
	
	return result;
}

#define LJ_CLEAN_PREVIOUS_VALUE(value) \
{ \
	free(value->strV); \
	value->strV = NULL; \
	\
	if (LJ_IS_CONTAINER(value)) { \
		json_value_release_tree(value->child); \
		value->child = NULL; \
	} \
}

/// double -> C string
char* ljftoa(const json_number_t input) {
	char* result = calloc(LJ_STRINGOPS_NUMMAX, sizeof(char));
	
	// TODO: optimize
	int32_t forceRound = (int32_t)input;
	
	// if there are no numbers after the comma, read it as int32_t
	if ((json_number_t)(forceRound) == input)
		sprintf(result, "%d", forceRound);
	else
		sprintf(result, "%lf", input);
		
	return result;
}

json_value_ref json_value_get_neighbor(json_value_ref base,
									   const json_index_t index,
									   const bool justCount,
									   json_index_t* countP) {
	json_index_t count = 0;
	json_value_ref current = base;
	
	while (current) {
		if (!justCount && count == index)
			return current; // the item we are looking for
	
		count++;
		
		if (justCount && !current->next)
			break; // return the last item instead
		
		current = current->next;
	}
	
	// save count
	LJ_IF_NOT_NULL(countP, count)
	return current;
}

json_value_ref json_value_find_by_key(json_value_ref base,
									  const char* key,
									  json_value_ref* previousP) {
	if (!base || !key)
		return NULL; // cannot start with NULL
		
	json_value_ref current = base;
	json_value_ref previous = NULL;
	
	while (current) {
		if (current->key && strcmp(current->key, key) == 0) {
			// found the one
			LJ_IF_NOT_NULL(previousP, previous)
			return current;
		}
		
		previous = current;
		current = current->next;
	}
	
	// not found
	return NULL;
}

char* lj_unescape_str(const char* input) {
	if (!input)
		return NULL;

	// TODO: optimize
	json_index_t resultSize = strlen(input) * 2 + 2;
	json_index_t resultLength = 1;
	char* result = calloc(resultSize, sizeof(char));
	
	// begin the resulting string with a double-quote
	result[0] = '"';
	
	for (json_index_t i = 0; i < strlen(input); i++) {
		char current = input[i];
		bool doEscape = false;
		
		switch (current) {
			case '\\':
			case '"': {
				doEscape = true;
				break;
			}
			case '\t': {
				current = 't';
				doEscape = true;
				break;
			}
			case '\r': {
				current = 'r';
				doEscape = true;
				break;
			}
			case '\n': {
				current = 'n';
				doEscape = true;
				break;
			}
			default:
				break;
		}
		
		if (doEscape)
			result[resultLength++] = '\\';
		
		result[resultLength++] = current;
	}
	
	// don't forget to close the string
	result[resultLength++] = '"';
	return result;
}

#define LJ_AUTOREALLOC_IF_NEEDED(condition, result, resultLength, resultSize) \
{ \
	if ((resultLength + condition + 2) >= resultSize) { \
		resultSize += condition + LJ_STRINGOPS_BUFSTEP; \
		result = realloc(result, sizeof(char) * resultSize); \
	} \
}

char* json_value_make_string_repr(const json_value_ref root,
								  json_index_t* lengthP,
								  const bool humanReadable,
								  const json_index_t baseSpaceCount) {
	// prepare resulting buffer
	json_index_t resultSize = LJ_STRINGOPS_BUFSTEP;
	json_index_t resultLength = 0;
	char* result = calloc(resultSize, sizeof(char));
	
	// used in humanReadable mode - space count
	json_index_t spaceCount = baseSpaceCount;
	
	if (LJ_IS_CONTAINER(root)) {
		// add the container beginning token first
		result[resultLength++] = (root->type == JSON_TYPE_ARRAY) ? '[' : '{';
		LJ_AUTOREALLOC_IF_NEEDED(2, result, resultLength, resultSize)
		
		spaceCount += LJ_STRINGOPS_TABSIZE;
		
		// add a new line before listing container items
		if (humanReadable)
			result[resultLength++] = '\n';
		
		// iterate through each child and add its stringified form
		json_value_ref child = root->child;
		
		while (child) {
			if (humanReadable) {
				// prepend the necessary spaces
				LJ_AUTOREALLOC_IF_NEEDED(spaceCount + 1, result, resultLength, resultSize)
				
				for (json_index_t sc = 0; sc < spaceCount; sc++)
					result[resultLength++] = ' ';
			}
		
			if (root->type == JSON_TYPE_OBJECT) {
				// need not to forget to add the keys
				char* escapedKey = lj_unescape_str(child->key);
				json_index_t escapedKeyLength = escapedKey ? strlen(escapedKey) : 0;
				
				ljprintf("escapedKey = %s", escapedKey);
				
				// allocate buffer space first
				LJ_AUTOREALLOC_IF_NEEDED(escapedKeyLength + 3, result, resultLength, resultSize)
				
				if (escapedKeyLength > 0) {
					// add the converted quoted key
					strcat(result, escapedKey);
					resultLength += escapedKeyLength;
					
					result[resultLength++] = ':';
					
					if (humanReadable)
						result[resultLength++] = ' ';
				}
			}
			
			// convert the child object into a document representation too
			json_index_t reprLength = 0;
			char* repr = json_value_make_string_repr(child, &reprLength, humanReadable, spaceCount);
			
			if (reprLength > 0) {
				// add the representation to our final string
				LJ_AUTOREALLOC_IF_NEEDED(reprLength + 3, result, resultLength, resultSize)
				
				strcat(result, repr);
				resultLength += reprLength;
			}
			
			child = child->next;
			
			// don't forget to add a comma seperating other values if there
			// are many of them
			if (child)
				result[resultLength++] = ',';
			
			// new line on each child item
			if (humanReadable)
				result[resultLength++] = '\n';
		}
		
		spaceCount -= LJ_STRINGOPS_TABSIZE;
		
		// end the container too
		LJ_AUTOREALLOC_IF_NEEDED(spaceCount + 1, result, resultLength, resultSize)
		
		if (humanReadable) {
			// prepend the last few spaces before container end
			for (json_index_t sc = 0; sc < spaceCount; sc++)
				result[resultLength++] = ' ';
		}
		
		result[resultLength++] = (root->type == JSON_TYPE_ARRAY) ? ']' : '}';
	} else {
		switch (root->type) {
			case JSON_TYPE_STRING: {
				// use its own quoted representation instead
				free(result);
				
				result = lj_unescape_str(root->strV);
				resultLength = result ? strlen(result) : 0;
				break;
			}
			case JSON_TYPE_NUMBER:
			case JSON_TYPE_BOOLEAN: {
				// use its strV variant
				free(result);
				
				result = strdup(root->strV);
				resultLength = strlen(result);
				break;
			}
			default: {
				// use null string
				strcpy(result, "null");
				resultLength = strlen(result);
				break;
			}
		}
	}
	
	LJ_IF_NOT_NULL(lengthP, resultLength)
	return result;
}

//
// json_value_ref API - public
//

json_value_ref json_value_init_string(const char* str) {
	if (!str) {
		ljprintf("NULL string provided as input, cannot continue");
		return NULL;
	}
	
	json_value_ref result = json_value_init(JSON_TYPE_STRING);
	json_value_set_string(result, str);
	
	return result;
}

json_value_ref json_value_init_number(const json_number_t num) {
	json_value_ref result = json_value_init(JSON_TYPE_NUMBER);
	json_value_set_number(result, num);
	
	return result;
}

json_value_ref json_value_init_boolean(const bool bv) {
	json_value_ref result = json_value_init(JSON_TYPE_BOOLEAN);
	json_value_set_boolean(result, bv);
	
	return result;
}

json_value_ref json_value_init_null() {
	json_value_ref result = json_value_init(JSON_TYPE_NULL);
	return result;
}

json_value_ref json_value_init_object() {
	json_value_ref result = json_value_init(JSON_TYPE_OBJECT);
	return result;
}

json_value_ref json_value_init_array() {
	json_value_ref result = json_value_init(JSON_TYPE_ARRAY);
	return result;
}

bool json_value_set_string(json_value_ref value, const char* str) {
	if (!value || !str) {
		ljprintf("value = <%p>, str = <%p>, something is NULL", value, str);
		return false;
	}
	
	// clean up previous values and set the appropriate type
	LJ_CLEAN_PREVIOUS_VALUE(value)
	value->type = JSON_TYPE_STRING;
	
	// set both string and numeric values
	value->strV = strdup(str);
	value->numV = ljatof(value->strV);
	return true;
}

bool json_value_set_number(json_value_ref value, const json_number_t num) {
	if (!value)
		return false;
		
	LJ_CLEAN_PREVIOUS_VALUE(value)
	value->type = JSON_TYPE_NUMBER;
	
	value->strV = ljftoa(value->numV);
	value->numV = num;
	return true;
}

bool json_value_set_boolean(json_value_ref value, const bool bv) {
	if (!value)
		return false;
		
	LJ_CLEAN_PREVIOUS_VALUE(value)
	value->type = JSON_TYPE_BOOLEAN;
	
	value->strV = bv ? strdup("true") : strdup("false");
	value->numV = bv;
	return true;
}

void json_value_release_tree(json_value_ref value) {
	if (!value)
		return;
		
	ljprintf("value <%p> type = %u awaiting release (tree)", value, value->type);
	
	// cache referenced values first
	json_value_ref child = value->child;
	json_value_ref next = value->next;
	
	// release itself
	json_value_release(value);
	
	// release all referenced children
	json_value_release_tree(child);
	// release all referenced neighboring items
	json_value_release_tree(next);
}

json_value_ref json_value_get(const json_value_ref container,
							  const char* key) {
	if (!container || container->type != JSON_TYPE_OBJECT || !key) {
		ljprintf("NULL cont./key or non-object specified as container <%p> [%u]", 
				 container, container->type);
		return NULL;
	}
	
	// get the first stored item first
	json_value_ref first = container->child;
	json_value_ref result = json_value_find_by_key(first, key, NULL);
	
	return result;
}

json_value_ref json_value_get_first(const json_value_ref container) {
	if (!container || !LJ_IS_CONTAINER(container))
		return NULL; // non-containers have no first/last item
		
	return container->child;
}

json_value_ref json_value_get_last(const json_value_ref container) {
	if (!container || !LJ_IS_CONTAINER(container))
		return NULL;
		
	json_value_ref last = container->child;
	last = json_value_get_neighbor(last, 0, true, NULL);
	
	return last;
}

json_value_ref json_value_get_at(const json_value_ref container,
								 const json_index_t where) {
	if (!container || !LJ_IS_CONTAINER(container))
		return NULL; // unavailable
		
	json_value_ref found = json_value_get_neighbor(container->child,
												   where, false, NULL);
	return found;
}

json_index_t json_value_get_count(const json_value_ref container) {
	if (!container)
		return 0;
	else if (!LJ_IS_CONTAINER(container))
		return 1; // only one item, which is self
	
	json_index_t result = 0;
	json_value_get_neighbor(container->child, 0, true, &result);
	
	return result;
}

bool json_value_set(const json_value_ref container, const char* key,
					json_value_ref value) {
	if (!container || !key || strlen(key) < 1 || 
		container->type != JSON_TYPE_OBJECT) {
		ljprintf("container <%p> or key <%p> is NULL or the former is not an object", 
				 container, key);
		return false;
	}
	
	// adjust soon-to-be-added value's key
	free(value->key);
	value->key = strdup(key);
	
	// get the last stored value if we have to append the new value
	json_value_ref last = json_value_get_last(container);
	
	// find an item that is named the same to maybe replace it
	json_value_ref foundBack = NULL;
	json_value_ref found = json_value_find_by_key(container->child, key, &foundBack);
	
	if (found) {
		ljprintf("found value, found = <%p>, key = \"%s\", type = %u",
				 found, found->key, found->type);
		ljprintf("foundBack = <%p>, key = \"%s\"", foundBack, 
				 foundBack ? foundBack->key : "-");
	
		// will be editing an existing value
		json_value_ref foundNext = found->next;
		
		// clean up this one
		found->next = NULL;
		json_value_release_tree(found);
		
		// adjust the newly added item's neighbor
		value->next = foundNext;
		
		if (foundBack)
			foundBack->next = value;
		
		if (container->child == found)
			container->child = value;
	} else if (last)
		last->next = value;
	else
		container->child = value;
		
	value->parent = container;
	return true;
}

const char* json_value_get_string(const json_value_ref value) {
	return (value ? value->strV : NULL);
}

json_number_t json_value_get_number(const json_value_ref value) {
	return (value ? value->numV : 0);
}

bool json_value_get_boolean(const json_value_ref value) {
	return (bool)(json_value_get_number(value));
}

const char* json_value_get_key(const json_value_ref value) {
	return (value ? value->key : NULL);
}

json_type_t json_value_get_type(const json_value_ref value) {
	return (value ? value->type : JSON_TYPE_NULL);
}

bool json_value_push(json_value_ref container, json_value_ref value) {
	if (!container || container->type != JSON_TYPE_ARRAY) {
		ljprintf("NULL or non-array container <%p> type = %u", container,
				 container->type);
		return false;
	} else if (!value) {
		ljprintf("NULL value specified");
		return false;
	}
	
	if (container->child) {
		// link to last value
		json_value_ref last = json_value_get_last(container);
		
		last->next = value;
		value->parent = container;
	} else {
		container->child = value;
		value->parent = container;
	}
	
	return true;
}

char* json_value_stringify(const json_value_ref container,
						   const bool humanReadable) {
	if (!container) {
		ljprintf("NULL root object provided");
		return NULL;
	}
	
	char* repr = json_value_make_string_repr(container, NULL, humanReadable, 0);
	return repr;
}

void json_value_release(json_value_ref value) {
	if (!value)
		return;
	
	ljprintf("value <%p> type = %u strV = \"%s\" awaiting release", 
			 value, value->type, value->strV);
	
	// release the only few manually managed values
	free(value->key);
	free(value->strV);
	
	// release itself
	free(value);
}