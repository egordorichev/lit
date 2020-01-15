#ifndef LIT_VALUE_H
#define LIT_VALUE_H

#include <lit/lit_common.h>
#include <lit/state/lit_state.h>
#include <lit/util/lit_array.h>

typedef double LitValue;

DECLARE_ARRAY(LitValues, LitValue, values)

void lit_print_value(LitValue value);

#endif