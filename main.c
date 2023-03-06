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
	
	char* repr = json_value_stringify(root, false);
	printf("repr:\n%s\n", repr);
	
	free(repr);
	json_value_release_tree(root);
	return 0;
}