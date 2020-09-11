#ifndef LIT_ERROR_H
#define LIT_ERROR_H

#include <lit/lit_common.h>
#include <lit/state/lit_state.h>
#include <lit/vm/lit_object.h>

#include <stdarg.h>

typedef enum LitError {
	// Scanner errors
	ERROR_UNEXPECTED_CHAR,
	ERROR_UNTERMINATED_STRING,
	ERROR_INVALID_ESCAPE_CHAR,
	ERROR_INTERPOLATION_NESTING_TOO_DEEP,
	ERROR_NUMBER_IS_TOO_BIG,
	ERROR_CHAR_EXPECTATION_UNMET,

	// Parser errors
	ERROR_EXPECTION_UNMET,
	ERROR_INVALID_ASSIGMENT_TARGET,
	ERROR_TOO_MANY_FUNCTION_ARGS,
	ERROR_MULTIPLE_ELSE_BRANCHES,
	ERROR_VAR_MISSING_IN_FORIN,
	ERROR_NO_GETTER_AND_SETTER,
	ERROR_STATIC_OPERATOR,
	ERROR_SELF_INHERITED_CLASS,
	ERROR_STATIC_FIELDS_AFTER_METHODS,
	ERROR_MISSING_STATEMENT,
	ERROR_EXPECTED_EXPRESSION,
	ERROR_DEFAULT_ARG_CENTRED,

	// Emitter errors
	ERROR_TOO_MANY_CONSTANTS,
	ERROR_TOO_MANY_PRIVATES,
	ERROR_VAR_REDEFINED,
	ERROR_TOO_MANY_LOCALS,
	ERROR_TOO_MANY_UPVALUES,
	ERROR_VARIABLE_USED_IN_INIT,
	ERROR_JUMP_TOO_BIG,
	ERROR_NO_SUPER,
	ERROR_THIS_MISSUSE,
	ERROR_SUPER_MISSUSE,
	ERROR_UNKNOWN_EXPRESSION,
	ERROR_UNKNOWN_STATEMENT,
	ERROR_LOOP_JUMP_MISSUSE,
	ERROR_RETURN_FROM_CONSTRUCTOR,
	ERROR_STATIC_CONSTRUCTOR,
	ERROR_CONSTANT_MODIFIED,
	ERROR_INVALID_REFERENCE_TARGET,

	ERROR_TOTAL
} LitError;

LitString* lit_vformat_error(LitState* state, uint line, LitError error, va_list args);
LitString* lit_format_error(LitState* state, uint line, LitError error, ...);

#endif