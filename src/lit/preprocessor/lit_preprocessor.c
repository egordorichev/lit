#include <lit/preprocessor/lit_preprocessor.h>
#include <lit/util/lit_utf.h>

#include <stdio.h>

bool lit_preprocess(LitState* state, char* source) {
	char c;

	char* macro_start = source;
	char* arg_start = source;
	char* current = source;

	bool in_macro = false;
	bool in_arg;

	do {
		c = current[0];
		current++;

		if (in_macro) {
			if (!lit_is_alpha(c) && !(((current - macro_start) > 1) && lit_is_digit(c))) {
				if (in_arg) {
					int size = (int) (current - macro_start) - 1;
					printf("arged '%.*s'\n", size, macro_start);

					// Remove the macro from code
					memset(macro_start - 1, ' ', size + 1);

					in_macro = false;
					in_arg = false;
				} else {
					int length = (int) (current - macro_start);

					if (memcmp(macro_start, "define", 6) == 0 || memcmp(macro_start, "ifdef", 5) == 0
						|| memcmp(macro_start, "ifndef", 6) == 0 || memcmp(macro_start, "undef", 5) == 0) {

						arg_start = current;
						in_arg = true;
					} else if (memcmp(macro_start, "else", 4) == 0 || memcmp(macro_start, "endif", 5) == 0) {
						int size = (int) (current - macro_start) - 1;
						printf("Unarged '%.*s'\n", size, macro_start);

						// Remove the macro from code
						memset(macro_start - 1, ' ', size + 1);
						in_macro = false;
					} else {
						// todo: proper error reporting
						printf("Unknown macro '%.*s'\n", (int) (current - macro_start) - 1, macro_start);
						return false;
					}
				}
			}
		} else {
			macro_start = current;
			in_macro = c == '#';
		}
	} while (c != '\0');

	if (macro_start != current) {
		// Fixme: proper error reporting
		printf("Unclosed macro!\n");
		return false;
	}

	return true;
}