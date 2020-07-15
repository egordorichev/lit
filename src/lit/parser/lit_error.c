#include <lit/parser/lit_error.h>
#include <stdio.h>

static const char* error_messages[ERROR_TOTAL];
static void setup_errors();
static bool errors_setup;

LitString* lit_vformat_error(LitState* state, uint line, LitError error, va_list args) {
	if (!errors_setup) {
		setup_errors();
	}

	int error_id = (int) error;
	const char* error_message = error_messages[error_id];

	va_list args_copy;
	va_copy(args_copy, args);
	size_t buffer_size = vsnprintf(NULL, 0, error_message, args_copy) + 1;
	va_end(args_copy);

	char buffer[buffer_size];

	vsnprintf(buffer, buffer_size, error_message, args);
	buffer[buffer_size - 1] = '\0';

	return AS_STRING(lit_string_format(state, "[err # line #]: $", (double) error_id, (double) line, (const char*) buffer));
}

LitString* lit_format_error(LitState* state, uint line, LitError error, ...) {
	va_list args;
	va_start(args, error);
	LitString* result = lit_vformat_error(state, line, error, args);
	va_end(args);

	return result;
}

static void setup_errors() {
	errors_setup = true;

	error_messages[ERROR_UNEXPECTED_CHAR] = "Unexpected character '%c'";
	error_messages[ERROR_UNTERMINATED_STRING] = "Unterminated string";
	error_messages[ERROR_INVALID_ESCAPE_CHAR] = "Invalid escape character '%c'";
	error_messages[ERROR_INTERPOLATION_NESTING_TOO_DEEP] = "Interpolation nesting is too deep, maximum is %i";
	error_messages[ERROR_NUMBER_IS_TOO_BIG] = "Number is too big to be represented by a single literal";
	error_messages[ERROR_CHAR_EXPECTATION_UNMET] = "Expected '%c' after '%c', got '%c'";
}