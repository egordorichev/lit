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
			LitClosure* closure = AS_CLOSURE(value);

			for (uint i = 0; i < closure->upvalue_count; i++) {
				if (closure->upvalues[i] == NULL) {
					printf(" ()");
					continue;
				} else if (AS_OBJECT(*closure->upvalues[i]->location) == closure) {
					printf(" (self)");
					continue;
				}

				printf(" ( ");
				lit_print_value(*closure->upvalues[i]->location);
				printf(" )");
			}

			break;
		}

		case OBJECT_NATIVE: {
			printf("native function");
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
			print_object(*AS_UPVALUE(value)->location);
			break;
		}

		case OBJECT_CLASS: {
			printf("class %s", AS_CLASS(value)->name->chars);
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