#include "util/lit_utf.h"
#include "std/lit_json.h"
#include "api/lit_api.h"
#include "state/lit_state.h"

typedef struct LitJsonValue {
	LitValue value;
	struct LitJsonValue* parent;
} LitJsonValue;

LitValue lit_json_parse(LitVm* vm, LitString* string) {
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

	for (const char* ch = string->chars; *ch != '\0'; ch++) {
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
				LitInstance* instance = lit_create_instance(vm->state, vm->state->object_class);

				if (IS_ARRAY(current)) {
					lit_values_write(vm->state, &AS_ARRAY(current)->values, OBJECT_VALUE(instance));
				} else if (IS_INSTANCE(current)) {
					lit_table_set(vm->state, &AS_INSTANCE(current)->fields, identifier, OBJECT_VALUE(instance));
				}

				current_value->value = current;
				current = OBJECT_VALUE(instance);
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
				} else if (IS_INSTANCE(current)) {
					lit_table_set(vm->state, &AS_INSTANCE(current)->fields, identifier, OBJECT_VALUE(array));
				}

				current_value->value = current;
				current = OBJECT_VALUE(array);
				parsing_value = true;
				expecting_identifier = true;

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
					lit_runtime_error_exiting(vm, "Unexpected string");
				}

				const char* identifier_start = ch + 1;

				do {
					ch++;
				} while (*ch != '"' && *ch != '\0');

				if (*ch == '\0') {
					FREE_ALL()
					lit_runtime_error_exiting(vm, "Unclosed string");
				}

				LitString* string = lit_copy_string(vm->state, identifier_start, ch - identifier_start);

				if (parsing_value) {
					parsing_value = false;
					expecting_identifier = false;

					if (IS_ARRAY(current)) {
						lit_values_write(vm->state, &AS_ARRAY(current)->values, OBJECT_VALUE(string));
					} else if (IS_INSTANCE(current)) {
						lit_table_set(vm->state, &AS_INSTANCE(current)->fields, identifier, OBJECT_VALUE(string));
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
					lit_runtime_error_exiting(vm, "Unexpected ':'");
				}

				expecting_colon = false;
				parsing_value = true;
				expecting_identifier = true;

				break;
			}

			case ',': {
				if (IS_ARRAY(current)) {
					parsing_value = true;
					expecting_identifier = true;
				} else if (IS_INSTANCE(current)) {
					expecting_identifier = true;
					parsing_value = false;
				}

				break;
			}

			case 't': {
				if (!parsing_value) {
					FREE_ALL()
					lit_runtime_error_exiting(vm, "Unexpected identifier");
				}

				if (*++ch == 'r') {
					if (*++ch == 'u') {
						if (*++ch == 'e') {
							if (IS_ARRAY(current)) {
								lit_values_write(vm->state, &AS_ARRAY(current)->values, BOOL_VALUE(true));
							} else if (IS_INSTANCE(current)) {
								lit_table_set(vm->state, &AS_INSTANCE(current)->fields, identifier, BOOL_VALUE(true));
							}

							expecting_identifier = false;
							parsing_value = false;

							break;
						}
					}
				}

				FREE_ALL()
				lit_runtime_error_exiting(vm, "Unexpected identifier");

				break;
			}

			case 'f': {
				if (!parsing_value) {
					FREE_ALL()
					lit_runtime_error_exiting(vm, "Unexpected identifier");
				}

				if (*++ch == 'a') {
					if (*++ch == 'l') {
						if (*++ch == 's') {
							if (*++ch == 'e') {
								if (IS_ARRAY(current)) {
									lit_values_write(vm->state, &AS_ARRAY(current)->values, BOOL_VALUE(false));
								} else if (IS_INSTANCE(current)) {
									lit_table_set(vm->state, &AS_INSTANCE(current)->fields, identifier, BOOL_VALUE(false));
								}

								expecting_identifier = false;
								parsing_value = false;

								break;
							}
						}
					}
				}

				FREE_ALL()
				lit_runtime_error_exiting(vm, "Unexpected identifier");

				break;
			}

			case 'n': {
				if (!parsing_value) {
					FREE_ALL()
					lit_runtime_error_exiting(vm, "Unexpected identifier");
				}

				if (*++ch == 'u') {
					if (*++ch == 'l') {
						if (*++ch == 'l') {
							if (IS_ARRAY(current)) {
								lit_values_write(vm->state, &AS_ARRAY(current)->values, NULL_VALUE);
							} else if (IS_INSTANCE(current)) {
								lit_table_set(vm->state, &AS_INSTANCE(current)->fields, identifier, NULL_VALUE);
							}

							expecting_identifier = false;
							parsing_value = false;

							break;
						}
					}
				}

				FREE_ALL()
				lit_runtime_error_exiting(vm, "Unexpected identifier");

				break;
			}

			default: {
				if (lit_is_digit(c)) {
					if (!parsing_value) {
						FREE_ALL()
						lit_runtime_error_exiting(vm, "Unexpected number");
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
					} else if (IS_INSTANCE(current)) {
						lit_table_set(vm->state, &AS_INSTANCE(current)->fields, identifier, NUMBER_VALUE(number));
					}

					expecting_identifier = false;
					ch--;
				} else {
					FREE_ALL()
					lit_runtime_error_exiting(vm, "Unexpected character '%c'", c);
				}

				break;
			}
		}
	}

	if (array_depth != 0) {
		FREE_ALL()
		lit_runtime_error_exiting(vm, "Unclosed '['");
	}

	if (object_depth != 0) {
		FREE_ALL()
		lit_runtime_error_exiting(vm, "Unclosed '{'");
	}

	#undef FREE_ALL

	lit_reallocate(vm->state, current_value, sizeof(struct LitJsonValue), 0);
	return OBJECT_VALUE(last_value);
}

LIT_METHOD(json_parse) {
	LIT_ENSURE_ARGS(1)

	if (!IS_STRING(args[0])) {
		lit_runtime_error_exiting(vm, "Argument #1 must be a string");
	}

	return lit_json_parse(vm, AS_STRING(args[0]));
}

LitString* json_map_to_string(LitVm* vm, LitValue instance, uint indentation) {
	indentation++;

	LitState* state = vm->state;
	LitMap* map = AS_INSTANCE(instance);
	LitTable* values = &map->values;

	if (values->count == 0) {
		return CONST_STRING(state, "{}");
	}

	bool has_wrapper = map->index_fn != NULL;
	uint value_amount = values->count;

	LitString* values_converted[value_amount];
	LitString* keys[value_amount];

	uint string_length = 2 + indentation;

	uint i = 0;
	uint index = 0;

	do {
		LitTableEntry* entry = &values->entries[index++];

		if (entry->key != NULL) {
			// Special hidden key
			LitValue field = has_wrapper ? map->index_fn(vm, map, entry->key, NULL) : entry->value;
			LitString* value = lit_json_to_string(state->vm, field, indentation);

			lit_push_root(state, (LitObject*) value);

			if (IS_STRING(field)) {
				value = AS_STRING(lit_string_format(state, "\"@\"", OBJECT_VALUE(value)));
			}

			values_converted[i] = value;
			keys[i] = entry->key;
			string_length += entry->key->length + 4 + value->length + (i == value_amount - 1 ? 1 : 2) + indentation;

			i++;
		}
	} while (i < value_amount);

	char buffer[string_length + 1];
	uint buffer_index = 2;

	memcpy(buffer, "{\n", 2);

	for (i = 0; i < value_amount; i++) {
		LitString *key = keys[i];
		LitString *value = values_converted[i];

		for (uint j = 0; j < indentation; j++) {
			buffer[buffer_index++] = '\t';
		}

		buffer[buffer_index++] = '\"';
		memcpy(&buffer[buffer_index], key->chars, key->length);
		buffer_index += key->length;
		buffer[buffer_index++] = '\"';

		memcpy(&buffer[buffer_index], ": ", 2);
		buffer_index += 2;

		memcpy(&buffer[buffer_index], value->chars, value->length);
		buffer_index += value->length;

		if (i == value_amount - 1) {
			buffer[buffer_index++] = '\n';

			for (uint j = 0; j < indentation - 1; j++) {
				buffer[buffer_index++] = '\t';
			}

			buffer[buffer_index++] = '}';
		} else {
			memcpy(&buffer[buffer_index], ",\n", 2);
			buffer_index += 2;
		}

		lit_pop_root(state);
	}

	return lit_copy_string(vm->state, buffer, buffer_index);
}

LitString* json_instance_to_string(LitVm* vm, LitValue instance, uint indentation) {
	indentation++;

	LitState* state = vm->state;
	LitInstance* object = AS_INSTANCE(instance);
	LitTable* values = &object->fields;

	if (values->count == 0) {
		return CONST_STRING(state, "{}");
	}

	uint value_amount = values->count;

	LitString* values_converted[value_amount];
	LitString* keys[value_amount];

	uint string_length = 2 + indentation;

	uint i = 0;
	uint index = 0;

	do {
		LitTableEntry* entry = &values->entries[index++];

		if (entry->key != NULL) {
			LitString* value = lit_json_to_string(state->vm, entry->value, indentation);

			lit_push_root(state, (LitObject*) value);

			if (IS_STRING(entry->value)) {
				value = AS_STRING(lit_string_format(state, "\"@\"", OBJECT_VALUE(value)));
			}

			values_converted[i] = value;
			keys[i] = entry->key;
			string_length += entry->key->length + 4 + value->length + (i == value_amount - 1 ? 1 : 2) + indentation;

			i++;
		}
	} while (i < value_amount);

	char buffer[string_length + 1];
	uint buffer_index = 2;

	memcpy(buffer, "{\n", 2);

	for (i = 0; i < value_amount; i++) {
		LitString *key = keys[i];
		LitString *value = values_converted[i];

		for (uint j = 0; j < indentation; j++) {
			buffer[buffer_index++] = '\t';
		}

		buffer[buffer_index++] = '\"';
		memcpy(&buffer[buffer_index], key->chars, key->length);
		buffer_index += key->length;
		buffer[buffer_index++] = '\"';

		memcpy(&buffer[buffer_index], ": ", 2);
		buffer_index += 2;

		memcpy(&buffer[buffer_index], value->chars, value->length);
		buffer_index += value->length;

		if (i == value_amount - 1) {
			buffer[buffer_index++] = '\n';

			for (uint j = 0; j < indentation - 1; j++) {
				buffer[buffer_index++] = '\t';
			}

			buffer[buffer_index++] = '}';
		} else {
			memcpy(&buffer[buffer_index], ",\n", 2);
			buffer_index += 2;
		}

		lit_pop_root(state);
	}

	return lit_copy_string(vm->state, buffer, buffer_index);
}

LitString* json_array_to_string(LitVm* vm, LitValue instance, uint indentation) {
	LitValues* values = &AS_ARRAY(instance)->values;
	LitState* state = vm->state;

	if (values->count == 0) {
		return CONST_STRING(state, "[]");
	}

	uint value_amount = values->count;
	LitString* values_converted[value_amount];

	uint string_length = 3; // "[ ]"

	for (uint i = 0; i < value_amount; i++) {
		LitValue field = values->values[i];
		LitString* value = lit_to_string(state, field, 0);

		lit_push_root(state, (LitObject*) value);

		if (IS_STRING(field)) {
			value = AS_STRING(lit_string_format(state, "\"@\"", OBJECT_VALUE(value)));
		}

		values_converted[i] = value;
		string_length += value->length + (i == value_amount - 1 ? 1 : 2);
	}

	char buffer[string_length + 1];
	memcpy(buffer, "[ ", 2);

	uint buffer_index = 2;

	for (uint i = 0; i < value_amount; i++) {
		LitString* part = values_converted[i];

		memcpy(&buffer[buffer_index], part->chars, part->length);
		buffer_index += part->length;

		memcpy(&buffer[buffer_index], (i == value_amount - 1) ? " ]" : ", ", 2);
		buffer_index += 2;

		lit_pop_root(state);
	}

	return lit_copy_string(vm->state, buffer, buffer_index);
}

LitString* lit_json_to_string(LitVm* vm, LitValue instance, uint indentation) {
	if (IS_MAP(instance)) {
		return json_map_to_string(vm, instance, indentation);
	} else if (IS_INSTANCE(instance)) {
		return json_instance_to_string(vm, instance, indentation);
	} else if (IS_ARRAY(instance)) {
		return json_array_to_string(vm, instance, indentation);
	} else if (IS_STRING(instance)) {
		return AS_STRING(instance);
	} else if (IS_NUMBER(instance) || IS_BOOL(instance)) {
		return lit_to_string(vm->state, instance, indentation);
	} else if (IS_NULL(instance)) {
		return CONST_STRING(vm->state, "null");
	}

	return CONST_STRING(vm->state, "invalid json");
}

LIT_METHOD(json_toString) {
	return OBJECT_VALUE(lit_json_to_string(vm, args[0], 0));
}

void lit_open_json_library(LitState* state) {
	LIT_BEGIN_CLASS("JSON")
		LIT_BIND_STATIC_METHOD("parse", json_parse)
		LIT_BIND_STATIC_METHOD("toString", json_toString)
	LIT_END_CLASS()
}