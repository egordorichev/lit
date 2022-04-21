#include "util/lit_utf.h"
#include "std/json/lit_json.h"
#include "api/lit_api.h"
#include "state/lit_state.h"

typedef struct LitJsonValue {
	LitValue value;
	struct LitJsonValue* parent;
} LitJsonValue;

LIT_METHOD(json_parse) {
	const char* string = LIT_CHECK_STRING(0);
	LitValue current = NULL_VALUE;
	LitValue last_value = NULL_VALUE;

	LitJsonValue* current_value = lit_reallocate(vm->state, NULL, 0, sizeof(struct LitJsonValue));
	current_value->parent = NULL;

	LitString* identifier = NULL;

	uint object_depth = 0;
	uint array_depth = 0;
	bool parsing_value = false;
	bool expecting_colon = false;
	bool expecting_identifier = false;

	#define FREE_ALL() \
		while (current_value != NULL) { \
			LitJsonValue* value = current_value; \
			current_value = current_value->parent; \
			lit_reallocate(vm->state, value, sizeof(struct LitJsonValue), 0); \
		}

	for (const char* ch = string; *ch != '\0'; ch++) {
		char c = *ch;

		switch (c) {
			case ' ':
			case '\r':
			case '\t':
			case '\n': {
				break;
			}

			case '{': {
				object_depth++;
				LitMap* map = lit_create_map(vm->state);

				if (IS_ARRAY(current)) {
					lit_values_write(vm->state, &AS_ARRAY(current)->values, OBJECT_VALUE(map));
				} else if (IS_MAP(current)) {
					lit_table_set(vm->state, &AS_MAP(current)->values, identifier, OBJECT_VALUE(map));
				}

				current_value->value = current;
				current = OBJECT_VALUE(map);
				parsing_value = false;
				expecting_identifier = true;

				LitJsonValue* value = lit_reallocate(vm->state, NULL, 0, sizeof(struct LitJsonValue));

				value->parent = current_value;
				current_value = value;

				break;
			}

			case '[': {
				array_depth++;
				LitArray* array = lit_create_array(vm->state);

				if (IS_ARRAY(current)) {
					lit_values_write(vm->state, &AS_ARRAY(current)->values, OBJECT_VALUE(array));
				} else if (IS_MAP(current)) {
					lit_table_set(vm->state, &AS_MAP(current)->values, identifier, OBJECT_VALUE(array));
				}

				current_value->value = current;
				current = OBJECT_VALUE(array);
				parsing_value = true;

				LitJsonValue* value = lit_reallocate(vm->state, NULL, 0, sizeof(struct LitJsonValue));

				value->parent = current_value;
				current_value = value;

				break;
			}

			case '}': {
				object_depth--;

				LitJsonValue* value = current_value;
				last_value = current;
				current_value = current_value->parent;
				current = current_value->value;
				lit_reallocate(vm->state, value, sizeof(struct LitJsonValue), 0);

				break;
			}

			case ']': {
				array_depth--;

				LitJsonValue* value = current_value;
				last_value = current;
				current_value = current_value->parent;
				current = current_value->value;
				lit_reallocate(vm->state, value, sizeof(struct LitJsonValue), 0);

				break;
			}

			case '"': {
				if (!expecting_identifier) {
					FREE_ALL()
					lit_runtime_error(vm, "Unexpected string");
				}

				const char* identifier_start = ch + 1;

				do {
					ch++;
				} while (*ch != '"' && *ch != '\0');

				if (*ch == '\0') {
					FREE_ALL()
					lit_runtime_error(vm, "Unclosed string");
				}

				LitString* string = lit_copy_string(vm->state, identifier_start, ch - identifier_start);

				if (parsing_value) {
					parsing_value = false;
					expecting_identifier = false;

					if (IS_ARRAY(current)) {
						lit_values_write(vm->state, &AS_ARRAY(current)->values, OBJECT_VALUE(string));
					} else if (IS_MAP(current)) {
						lit_table_set(vm->state, &AS_MAP(current)->values, identifier, OBJECT_VALUE(string));
					}
				} else {
					expecting_colon = true;
					identifier = string;
				}

				break;
			}

			case ':': {
				if (!expecting_colon) {
					FREE_ALL()
					lit_runtime_error(vm, "Unexpected ':'");
				}

				expecting_colon = false;
				parsing_value = true;
				expecting_identifier = true;

				break;
			}

			case ',': {
				if (IS_ARRAY(current)) {
					parsing_value = true;
				} else if (IS_MAP(current)) {
					expecting_identifier = true;
					parsing_value = false;
				}

				break;
			}

			default: {
				if (lit_is_digit(c)) {
					if (!parsing_value) {
						FREE_ALL()
						lit_runtime_error(vm, "Unexpected number");
					}

					const char* number_start = ch;

					while (lit_is_digit(*ch)) {
						ch++;
					}

					// Look for a fractional part.
					if (*ch == '.' && lit_is_digit(*(ch + 1))) {
						// Consume the '.'
						ch++;

						while (lit_is_digit(*ch)) {
							ch++;
						}
					}

					double number = strtod(number_start, NULL);

					if (IS_ARRAY(current)) {
						lit_values_write(vm->state, &AS_ARRAY(current)->values, NUMBER_VALUE(number));
					} else if (IS_MAP(current)) {
						lit_table_set(vm->state, &AS_MAP(current)->values, identifier, NUMBER_VALUE(number));
					}

					expecting_identifier = false;
					ch--;
				} else {
					FREE_ALL()
					lit_runtime_error(vm, "Unexpected character '%c'", c);
				}

				break;
			}
		}
	}

	if (array_depth != 0) {
		FREE_ALL()
		lit_runtime_error(vm, "Unclosed '['");
	}

	if (object_depth != 0) {
		FREE_ALL()
		lit_runtime_error(vm, "Unclosed '{'");
	}

	#undef FREE_ALL

	lit_reallocate(vm->state, current_value, sizeof(struct LitJsonValue), 0);
	return OBJECT_VALUE(last_value);
}

void lit_open_json_library(LitState* state) {
	LIT_BEGIN_CLASS("JSON")
		LIT_BIND_STATIC_METHOD("parse", json_parse)
	LIT_END_CLASS()
}