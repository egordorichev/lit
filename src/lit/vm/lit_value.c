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
			printf("function %s", AS_FUNCTION(value)->name->chars);
			break;
		}

		case OBJECT_CLOSURE: {
#ifdef LIT_TRACE_STACK
			printf("closure %s", AS_CLOSURE(value)->function->name->chars);
#else
			printf("function %s", AS_CLOSURE(value)->function->name->chars);
#endif
			break;
		}

		case OBJECT_NATIVE_PRIMITIVE: {
			printf("function %s", AS_NATIVE_PRIMITIVE(value)->name->chars);
			break;
		}

		case OBJECT_NATIVE_FUNCTION: {
			printf("function %s", AS_NATIVE_FUNCTION(value)->name->chars);
			break;
		}

		case OBJECT_PRIMITIVE_METHOD: {
			printf("function %s", AS_PRIMITIVE_METHOD(value)->name->chars);
			break;
		}

		case OBJECT_NATIVE_METHOD: {
			printf("function %s", AS_NATIVE_METHOD(value)->name->chars);
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
			lit_print_value(AS_BOUND_METHOD(value)->method);
			return;
		}

		case OBJECT_ARRAY: {
			#ifdef LIT_MINIMIZE_CONTAINERS
				printf("array");
			#else
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
			#endif
			break;
		}

		case OBJECT_MAP: {
			#ifdef LIT_MINIMIZE_CONTAINERS
				printf("map");
			#else
				LitMap* map = AS_MAP(value);
				uint size = map->values.count;
				printf("(%u) {", size);
				bool had_before = false;

				if (size > 16) {
					printf(" (too big to be displayed) ");
				} else if (size > 0) {
					for (int i = 0; i < map->values.capacity; i++) {
						LitTableEntry* entry = &map->values.entries[i];

						if (entry->key != NULL) {
							if (had_before) {
								printf(", ");
							} else {
								printf(" ");
							}

							printf("%s = ", entry->key->chars);
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
			#endif
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

		case OBJECT_REFERENCE: {
			printf("reference => ");
			LitValue* slot = AS_REFERENCE(value)->slot;

			if (slot == NULL) {
				printf("null");
			} else {
				lit_print_value(*slot);
			}

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

const char* lit_get_value_type(LitValue value) {
	if (IS_BOOL(value)) {
		return "bool";
	} else if (IS_NULL(value)) {
		return "null";
	} else if (IS_NUMBER(value)) {
		return "number";
	} else if (IS_OBJECT(value)) {
		return lit_object_type_names[OBJECT_TYPE(value)];
	} else {
		UNREACHABLE
	}

	return "unknown";
}