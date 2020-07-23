#include <lit/vm/lit_value.h>
#include <lit/mem/lit_mem.h>
#include <lit/vm/lit_object.h>

#include <stdio.h>

DEFINE_ARRAY(LitValues, LitValue, values)

static void print_object(LitValue value) {
	switch (OBJECT_TYPE(value)) {
		case OBJECT_STRING: {
			printf("%s", AS_CSTRING(value));
			break;
		}

		case OBJECT_FUNCTION: {
			LitString* name = AS_FUNCTION(value)->name;
			printf("function %s", name == NULL ? "unknown" : name->chars);
			break;
		}

		case OBJECT_CLOSURE: {
			printf("function %s", AS_CLOSURE(value)->function->name->chars);
			break;
		}

		case OBJECT_NATIVE_PRIMITIVE:
		case OBJECT_NATIVE_FUNCTION: {
			printf("native function");
			break;
		}

		case OBJECT_PRIMITIVE_METHOD:
		case OBJECT_NATIVE_METHOD: {
			printf("native method");
			break;
		}

		case OBJECT_FIBER: {
			printf("fiber");
			break;
		}

		case OBJECT_MODULE: {
			printf("module %s", AS_MODULE(value)->name->chars);
			break;
		}

		case OBJECT_UPVALUE: {
			LitUpvalue* upvalue = AS_UPVALUE(value);

			if (upvalue->location == NULL) {
				lit_print_value(upvalue->closed);
			} else {
				print_object(*upvalue->location);
			}

			break;
		}

		case OBJECT_CLASS: {
			printf("class %s", AS_CLASS(value)->name->chars);
			break;
		}

		case OBJECT_INSTANCE: {
			printf("%s instance", AS_INSTANCE(value)->klass->name->chars);
			break;
		}

		case OBJECT_BOUND_METHOD: {
			LitValue method = AS_BOUND_METHOD(value)->method;

			if (IS_FUNCTION(method)) {
				printf("method %s", AS_FUNCTION(method)->name->chars);
			} else {
				printf("native method");
			}

			break;
		}

		case OBJECT_ARRAY: {
			LitArray* array = AS_ARRAY(value);
			uint size = array->values.count;

			printf("(%u) [", size);

			if (size > 32) {
				printf(" (too big to be displayed) ");
			} else if (size > 0) {
				printf(" ");

				for (uint i = 0; i < size; i++) {
					lit_print_value(array->values.values[i]);

					if (i + 1 < size) {
						printf(", ");
					} else {
						printf(" ");
					}
				}
			}

			printf("]");
			break;
		}

		case OBJECT_MAP: {
			LitMap* map = AS_MAP(value);
			uint size = map->values.count;
			printf("(%u) {", size);
			bool had_before = false;

			if (size > 16) {
				printf(" (too big to be displayed) ");
			} else if (size > 0) {
				for (uint i = 0; i < map->values.capacity; i++) {
					LitTableEntry* entry = &map->values.entries[i];

					if (entry->key != NULL) {
						if (had_before) {
							printf(", ");
						} else {
							printf(" ");
						}

						printf("\"%s\" : ", entry->key->chars);
						lit_print_value(entry->value);
						had_before = true;
					}
				}
			}

			if (had_before) {
				printf(" }");
			} else {
				printf("}");
			}

			break;
		}

		case OBJECT_USERDATA: {
			printf("userdata");
			break;
		}

		case OBJECT_RANGE: {
			LitRange* range = AS_RANGE(value);
			printf("%g .. %g", range->from, range->to);

			break;
		}

		case OBJECT_FIELD: {
			printf("field");
			break;
		}

		default: {
			UNREACHABLE
		}
	}
}

void lit_print_value(LitValue value) {
	if (IS_BOOL(value)) {
		printf(AS_BOOL(value) ? "true" : "false");
	} else if (IS_NULL(value)) {
		printf("null");
	} else if (IS_NUMBER(value)) {
		printf("%g", AS_NUMBER(value));
	} else if (IS_OBJECT(value)) {
		print_object(value);
	} else {
		UNREACHABLE
	}
}

void lit_values_ensure_size(LitState* state, LitValues* values, uint size) {
	if (values->capacity < size) {
		uint old_capacity = values->capacity;
		values->capacity = size;
		values->values = LIT_GROW_ARRAY(state, values->values, LitValue, old_capacity, size);

		for (uint i = old_capacity; i < size; i++) {
			values->values[i] = NULL_VALUE;
		}
	}

	if (values->count < size) {
		values->count = size;
	}
}