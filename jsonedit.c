#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "litejson.h"

#define READ_STDIN_MIN 12

#define IS_HELP(option) (tolower(option[1]) == 'h' || option[1] == '?')
#define IS_AN_OPTION(str) (strlen(str) >= 2 && str[0] == '-')

#define AUTOEXTEND_BUFFER(str, count, size) \
{ \
	if ((count + 3) >= size) { \
		size += READ_STDIN_MIN; \
		str = realloc(str, sizeof(char) * size); \
	} \
}

char* read_stdin(void) {
	// prepare our autoextendable buffer
	json_index_t size = READ_STDIN_MIN;
	json_index_t count = 0;
	char* result = calloc(size, sizeof(char));

	while (feof(stdin) == 0) {
		char current = fgetc(stdin);

		// save current character
		result[count++] = current;
		result[count] = '\0';

		// need to autoextend to prepare for more data
		AUTOEXTEND_BUFFER(result, count, size)
	}

	return result;
}

/// reads the specified file path into a C string
char* read_file(const char* fn) {
	if (!fn || (strlen(fn) < 2 && fn[0] == '-'))
		return read_stdin(); // reading from stdin in this case

	FILE* stream = fopen(fn, "r");

	if (!stream) {
		// no luck, sorry (or the file doesn't exist)
		perror("Failed to open file for reading!!");
		return NULL;
	}

	// first detect file size
	json_index_t size = 0;

	fseek(stream, 0L, SEEK_END);
	size = ftell(stream);

	rewind(stream);

	// now read in everything into a buffer
	char* result = calloc(size + 1, sizeof(char));
	fread(result, sizeof(char), size, stream);

	// clean up
	fclose(stream);
	return result;
}

char** split_get_query(const char* query, json_index_t* countP) {
	json_index_t count = 0;
	json_index_t size = READ_STDIN_MIN;
	char** parts = calloc(size, sizeof(char*));

	// again, autoextendable token components
	json_index_t tCount, tSize = 0;
	char* token = NULL;

	// inside '\\'
	bool insideEscape = false;

	for (json_index_t i = 0; i < strlen(query); i++) {
		if (!token) {
			tCount = 0;
			tSize = READ_STDIN_MIN;
			token = calloc(tSize, sizeof(char));
		}

		char current = query[i];

		if (current == '.' && !insideEscape) {
			// end of token
			if (tCount >= 1)
				parts[count++] = token;
			else
				free(token); // empty token skipped
			token = NULL;
		} else {
			if (current == '\\' && !insideEscape) {
				// handle '\\'
				insideEscape = true;
				continue;
			}

			insideEscape = false;

			token[tCount++] = current;
			token[tCount] = '\0';
		}

		// autoextensions

		if ((count + 1) >= size) {
			size += READ_STDIN_MIN;
			parts = realloc(parts, sizeof(char*));
		}

		AUTOEXTEND_BUFFER(token, tCount, tSize)
	}

	// add last stray token to parts
	if (token && tCount >= 1)
		parts[count++] = token;

	if (countP)
		(*countP) = count;

	return parts;
}

void free_string_array(char** input, const json_index_t count) {
	for (json_index_t i = 0; i < count; i++)
		free(input[i]);

	free(input);
}

bool is_a_number(const char* str) {
	if (!str || strlen(str) < 1)
		return false;
	else {
		for (json_index_t i = 0; i < strlen(str); i++) {
			const char current = str[i];

			if (current != '.' && current != '-' && current != '+' &&
			    isdigit(current) == 0)
			    	return false;
		}

		return true;
	}
}

json_value_ref find_json_value(json_value_ref root, const char* query) {
	// split query into parts first
	json_index_t partsCount = 0;
	char** parts = split_get_query(query, &partsCount);

	if (partsCount < 1)
		return NULL; // invalid query

	json_value_ref current = root;

	for (json_index_t level = 0; level < partsCount; level++) {
		char* label = parts[level];

		if (is_a_number(label)) {
			// this is an index, need to get the relevant item
			int index = atoi(label);

			if (json_value_get_type(current) == JSON_TYPE_ARRAY &&
			    index >= 0) {
				json_value_ref first = json_value_get_at(current, (json_index_t)index);

				if (!first) {
					// out of bounds
					fprintf(stderr, "Index out of bounds inside '%s' - asked for %u",
						json_value_get_key(current), (json_index_t)index);

					// clean up and fail
					free_string_array(parts, partsCount);
					return NULL;
				} else {
					// found it
					current = first;
					continue;
				}
			}
		} else if (strlen(label) >= 2 && label[0] == '@') {
			// TODO: implement special keys
			fprintf(stderr, "TODO for '%s'\n", label);
			
			free_string_array(parts, partsCount);
			return NULL;
		}

		// otherwise look for the specified key
		json_value_ref found = json_value_get(current, label);

		if (!found) {
			if (json_value_get_type(current) != JSON_TYPE_OBJECT)
				fprintf(stderr, "\"%s\" is not an object, can't look for \"%s\"",
						json_value_get_key(current),
						label);
			else
				fprintf(stderr, "No item labeled \"%s\" inside \"%s\".",
						label,
						json_value_get_key(current));

			// clean up and exit
			free_string_array(parts, partsCount);
			return NULL;
		}

		current = found;
	}

	free_string_array(parts, partsCount);
	return current;
}

int show_help(const char* identity) {
	fprintf(stderr, "Usage: %s -get KEY1.KEY2 FILENAME\n", identity);
	fprintf(stderr, "       %s -help\n", identity);
	return 1;
}

int main(const int argc, const char** argv) {
	const char* option = argv[1];

	if (argc < 4 || !IS_AN_OPTION(option) || IS_HELP(option))
		return show_help(argv[0]);

	const char* query = argv[2];
	const char* filename = argv[3];

	// read in file contents and parse it first
	char* raw = read_file(filename);
	if (!raw)
		return 1; // fail

	json_error error;
	json_value_ref root = json_parse(raw, &error);

	if (error.fail) {
		fprintf(stderr, "Parsing error - line %u, character %u, %s\n",
				error.line, error.character, error.message);

		// clean up and exit
		free(raw);
		return 2;
	}

	free(raw);

	switch (option[1]) {
		case 'g': {
			// -get
			json_value_ref result = find_json_value(root, query);

			if (!result) {
				// not found
				json_value_release_tree(root);
				return 3;
			}

			json_type_t type = json_value_get_type(result);

			switch (type) {
				case JSON_TYPE_ARRAY:
				case JSON_TYPE_OBJECT: {
					// containers need stringification first
					char* strV = json_value_stringify(result, false);
					printf("%s\n", strV);

					free(strV);
					break;
				}
				default: {
					printf("%s\n", json_value_get_string(result));
					break;
				}
			}

			break;
		}
		default:
			break;
	}

	// clean up
	json_value_release_tree(root);
	return 0;
}
