#include "lit/preprocessor/lit_preprocessor.h"
#include "lit/vm/lit_object.h"
#include "lit/util/lit_utf.h"
#include "lit/state/lit_state.h"
#include "lit/parser/lit_error.h"

#include <stdio.h>

void lit_init_preprocessor(LitState* state, LitPreprocessor* preprocessor) {
	preprocessor->state = state;

	lit_init_table(&preprocessor->defined);
	lit_init_values(&preprocessor->open_ifs);
}

void lit_free_preprocessor(LitPreprocessor* preprocessor) {
	lit_free_table(preprocessor->state, &preprocessor->defined);
	lit_free_values(preprocessor->state, &preprocessor->open_ifs);
}

void lit_add_definition(LitState* state, const char* name) {
	lit_table_set(state, &state->preprocessor->defined, CONST_STRING(state, name), TRUE_VALUE);
}

static void override(char* source, int length) {
	while (length-- > 0) {
		if (*source != '\n') {
			*source = ' ';
		}
		
		source++;
	}
}

bool lit_preprocess(LitPreprocessor* preprocessor, char* source) {
	LitState* state = preprocessor->state;
	LitValue tmp;

	char c;

	char* macro_start = source;
	char* arg_start = source;
	char* current = source;

	bool in_macro = false;
	bool in_arg = false;
	bool on_new_line = true;

	int ignore_depth = -1;
	int depth = 0;

	do {
		c = current[0];
		current++;

		// Single line comment
		if (c == '/' && current[0] == '/') {
			current++;

			do {
				c = current[0];
				current++;
			} while (c != '\n' && c != '\0');

			in_macro = false;
			on_new_line = true;

			continue;
		} else if (c == '/' && current[0] == '*') {
			// Multiline comment
			current++;

			do {
				c = current[0];
				current++;
			} while (c != '*' && c != '\0' && current[0] != '/');

			in_macro = false;
			on_new_line = true;

			continue;
		}

		if (in_macro) {
			if (!lit_is_alpha(c) && !(((current - macro_start) > 1) && lit_is_digit(c))) {
				if (in_arg) {
					LitString* arg = lit_copy_string(state, arg_start, (int) (current - arg_start) - 1);

					if (memcmp(macro_start, "define", 6) == 0 || memcmp(macro_start, "undef", 5) == 0) {
						if (ignore_depth < 0) {
							bool close = macro_start[0] == 'u';

							if (close) {
								lit_table_delete(&preprocessor->defined, arg);
							} else {
								lit_table_set(state, &preprocessor->defined, arg, TRUE_VALUE);
							}
						}
					} else { // ifdef || ifndef
						depth++;

						if (ignore_depth < 0) {
							bool close = macro_start[2] == 'n';

							if ((lit_table_get(&preprocessor->defined, arg, &tmp) ^ close) == false) {
								ignore_depth = depth;
							}

							lit_values_write(preprocessor->state, &preprocessor->open_ifs, (LitValue) macro_start);
						}
					}

					// Remove the macro from code
					override(macro_start - 1, (int) (current - macro_start));

					in_macro = false;
					in_arg = false;
				} else {
					int length = (int) (current - macro_start);

					if (memcmp(macro_start, "define", 6) == 0 || memcmp(macro_start, "undef", 5) == 0
						|| memcmp(macro_start, "ifdef", 5) == 0 || memcmp(macro_start, "ifndef", 6) == 0) {

						arg_start = current;
						in_arg = true;
					} else if (memcmp(macro_start, "else", 4) == 0 || memcmp(macro_start, "endif", 5) == 0) {
						in_macro = false;

						// If this is endif
						if (macro_start[1] == 'n') {
							depth--;

							if (ignore_depth > -1) {
								// Remove the whole if branch from code
								char* branch_start = (char*) preprocessor->open_ifs.values[preprocessor->open_ifs.count - 1];
								override(branch_start - 1, (int) (current - branch_start));

								if (ignore_depth == depth + 1) {
									ignore_depth = -1;
									preprocessor->open_ifs.count--;
								}
							} else {
								preprocessor->open_ifs.count--;

								// Remove #endif
								override(macro_start - 1, (int) (current - macro_start));
							}
						} else if (ignore_depth < 0 || depth <= ignore_depth) {
							// #else
							if (ignore_depth == depth) {
								// Remove the macro from code
								char* branch_start = (char*) preprocessor->open_ifs.values[preprocessor->open_ifs.count - 1];
								override(branch_start - 1, (int) (current - branch_start));

								ignore_depth = -1;
							} else {
								preprocessor->open_ifs.values[preprocessor->open_ifs.count - 1] = (LitValue) macro_start;
								ignore_depth = depth;
							}
						}
					} else {
						lit_error(preprocessor->state, 0, lit_format_error(preprocessor->state, 0, ERROR_UNKNOWN_MACRO, (int) (current - macro_start) - 1, macro_start)->chars);
						return false;
					}
				}
			}
		} else {
			macro_start = current;

			if (c == '\n') {
				on_new_line = true;
			} else if (!(c == '\t' || c == ' ' || c == '#')) {
				on_new_line = false;
			} else {
				in_macro = on_new_line && c == '#';
			}
		}
	} while (c != '\0');

	if (in_macro || preprocessor->open_ifs.count > 0 || depth > 0) {
		lit_error(preprocessor->state, 0, lit_format_error(preprocessor->state, 0, ERROR_UNCLOSED_MACRO)->chars);
		return false;
	}

	lit_free_values(preprocessor->state, &preprocessor->open_ifs);
	return true;
}