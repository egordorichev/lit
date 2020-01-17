#include <lit/vm/lit_value.h>
#include <lit/mem/lit_mem.h>

#include <stdio.h>

DEFINE_ARRAY(LitValues, LitValue, values)

static void print_object(LitValue value) {

}

void lit_print_value(LitValue value) {
	if (IS_BOOL(value)) {
		printf(AS_BOOL(value) ? "true" : "false");
	} else if (IS_NULL(value)) {
		printf("null");
	} else if (IS_NUMBER(value)) {
		printf("%g", AS_NUMBER(value));
	} else if (IS_OBJ(value)) {
		print_object(value);
	}
}