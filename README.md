# LiteJSON
_A tiny JSON parsing/generation library written in C99 without any other dependencies._

**Copyright © Tim K. 2023 <timk@xfen.page>**

**LiteJSON** is a very minimalistic library written in C that allows processing and generating data stored in JSON format.  It has no external dependencies and no platform-dependent code, which makes it a truly portable library supporting a variety of platforms.

## Building

LiteJSON is always compiled as a static library. To build it as one, just run this command in the sources directory:

```bash
$ make RELEASE=1
```

And then LiteJSON will be stored in ``liblitejson.a``.

Alternatively, you can just add ``litejson.h`` and ``litejson.c`` straight to your C project without even linking it into a static library. Try it, it works!

## Tutorial

Please see ``litejson.h`` for public API documentation.

Here is an example on how to parse a JSON string:

```c
#include <stdio.h>
#include <stdlib.h>
#include <litejson.h>

int main() {
	// our test JSON document
	const char* demo = "{ \"title\": \"Shopping List\","
			   "  \"items\": [\"crisps\", \"milk\"] }";

	//
	// json_error is a structure containing details relating to a
	// parsing error. If json_parse returns NULL, then the 
	// error.fail flag will be set to true and all the other fields
	// of the structure will contain valid data
	//
	json_error error;
	json_value_ref root = json_parse(demo, &error);

	if (error.fail) {
		// looks like parsing failed, let's dump the error to
		// the screen

		printf("at line %u character %u:\n", error.line,
						     error.character);
		printf(" %s\n", error.message);

		// we have to do this to avoid memory leaks
		free(error.message);
		return 1;
	}

	// now that the document is loaded, we can explore it!

	// this will receive the value of "title"
	json_value_ref titleValue = json_value_get(root, "title");

	if (titleValue) {
		// json_value_get() returns NULL if the item doesn't
		// exist
		printf("%s\n", json_value_get_string(titleValue));
	}

	// let's try adding a new field to our document
	json_value_ref doneValue = json_value_init_boolean(false);
	json_value_set(root, "done", doneValue);

	// we can now stringify our final document and see what it
	// looks like
	char* doc = json_value_stringify(root, true);
	printf("%s\n", doc);

	// clean up
	free(doc);
	// we need to use json_value_release_tree() to deallocate all
	// document-related values at once
	json_value_release_tree(root);

	//
	// we MUST NOT deallocate doneValue or titleValue, as when we
	// set or receive json_value_ref instances, we receive them
	// directly and not as copies, so all the memory management
	// is "linked" and "done" by their parent object
	//

	return 0;
}
```

Compile it:

```bash
$ cc -o demo -std=c99 -I. demo.c -L. -llitejson
```

And run it:

```bash 
$ ./demo
```

The result should be:

```json
Shopping List
{
   "title": "Shopping List",
   "items": [
      "crisps",
      "milk"
   ],
   "done": false
}
```

## License

The project is licensed under Zero-Clause BSD License (0BSD).