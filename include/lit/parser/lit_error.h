#ifndef LIT_ERROR_H
#define LIT_ERROR_H

#include <lit/lit_common.h>
#include <lit/state/lit_state.h>
#include <lit/vm/lit_object.h>

#include <stdarg.h>

typedef enum LitError {
	ERROR_UNEXPECTED_CHAR,
	ERROR_UNTERMINATED_STRING,
	ERROR_INVALID_ESCAPE_CHAR,
	ERROR_INTERPOLATION_NESTING_TOO_DEEP,
	ERROR_NUMBER_IS_TOO_BIG,
	ERROR_CHAR_EXPECTATION_UNMET,

	ERROR_TOTAL
} LitError;

LitString* lit_vformat_error(LitState* state, uint line, LitError error, va_list args);
LitString* lit_format_error(LitState* state, uint line, LitError error, ...);

#endif