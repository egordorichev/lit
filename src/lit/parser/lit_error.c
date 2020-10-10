#include "parser/lit_error.h"
#include <stdio.h>

static const char* error_messages[ERROR_TOTAL] = {
	"Unclosed macro.",
	"Unknown macro '%.*s'.",
	"Unexpected character '%c'",
	"Unterminated string",
	"Invalid escape character '%c'",
	"Interpolation nesting is too deep, maximum is %i",
	"Number is too big to be represented by a single literal",
	"Expected '%c' after '%c', got '%c'",
	"Expected %s, got '%.*s'",
	"Invalid assigment target",
	"Function can't have more than 255 arguments, got %i",
	"If-statement can have only one else-branch",
	"For-loops using in-iteration must declare a new variable",
	"Expected declaration of either getter or setter, got none",
	"Operator methods can't be static or defined in static classes",
	"Class can't inherit itself",
	"All static fields must be defined before the methods",
	"Expected statement but got nothing",
	"Expected expression after '%.*s', got '%.*s'",
	"Default arguments must always be in the end of the argument list.",
	"Too many constants for one chunk",
	"Too many private locals for one module",
	"Variable '%.*s' was already declared in this scope",
	"Too many local variables for one function",
	"Too many upvalues for one function",
	"Variable '%.*s' can't use itself in its initializer",
	"Too much code to jump over",
	"'super' can't be used in class '%s', because it doesn't have a super class",
	"'this' can't be used %s",
	"'super' can't be used %s",
	"Unknown expression with id '%i'",
	"Unknown statement with id '%i'",
	"Can't use '%s' outside of loops",
	"Can't use 'return' in constructors",
	"Constructors can't be static (at least for now)",
	"Attempt to modify constant '%.*s'",
	"Invalid refence target"
};

LitString* lit_vformat_error(LitState* state, uint line, LitError error, va_list args) {
	int error_id = (int) error;
	const char* error_message = error_messages[error_id];

	va_list args_copy;
	va_copy(args_copy, args);
	size_t buffer_size = vsnprintf(NULL, 0, error_message, args_copy) + 1;
	va_end(args_copy);

	char buffer[buffer_size];

	vsnprintf(buffer, buffer_size, error_message, args);
	buffer[buffer_size - 1] = '\0';

	if (line != 0) {
		return AS_STRING(lit_string_format(state, "[err # line #]: $", (double) error_id, (double) line, (const char*) buffer));
	}

	return AS_STRING(lit_string_format(state, "[err #]: $", (double) error_id, (const char*) buffer));
}

LitString* lit_format_error(LitState* state, uint line, LitError error, ...) {
	va_list args;
	va_start(args, error);
	LitString* result = lit_vformat_error(state, line, error, args);
	va_end(args);

	return result;
}