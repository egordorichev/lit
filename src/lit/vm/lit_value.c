#include <lit/vm/lit_value.h>
#include <lit/mem/lit_mem.h>

#include <stdio.h>

DEFINE_ARRAY(LitValues, LitValue, values)

void lit_print_value(LitValue value) {
	printf("%lf", value);
}