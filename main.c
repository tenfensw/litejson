#include <stdio.h>
#include "litejson.h"

void gen_pointing_arrow(const json_index_t offset) {
	for (json_index_t i = 1; i < offset; i++)
		printf("%c", ' ');
	
	printf("%c\n", '^');
}

int main() {
	const char* input = "{ \"hello\": null, \"world\": \"aboba\", \"foo\": { \"bar\": true, \"coffeeLove\": [\"espresso\", \"cappuchino\", {\"rating\": 5}] }, \"baz\": 1 }";
	
	json_error result;
	json_value_ref root = json_parse(input, &result);
	
	if (result.fail) {
		printf("FAIL!!\n");
		printf("Msg: %s\n", result.message);
		printf("Line: %u\nCharacter: %u\n", result.line, result.character);
		printf("\n%s\n", input);
		gen_pointing_arrow(result.character);
		
		return 1;
	}
	
	printf("root count: %u\n", json_value_get_count(root));
	
	// change "world" value to a number
	json_value_ref newWorldValue = json_value_init_number(5454);
	json_value_set(root, "world", newWorldValue);
	
	// add a new "reviewed" value
	json_value_ref reviewedValue = json_value_init_boolean(true);
	json_value_set(root, "reviewed", reviewedValue);
	
	// get value at pos #2
	json_value_ref positionTwo = json_value_get_at(root, 2);
	printf("positionTwo = <%p>, key = \"%s\", type = %u\n",
		   positionTwo, json_value_get_key(positionTwo),
		   json_value_get_type(positionTwo));
	
	char* repr = json_value_stringify(root, true);
	printf("repr:\n%s\n", repr);
	
	free(repr);
	json_value_release_tree(root);
	return 0;
}