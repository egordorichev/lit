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

	// Scanner errors
	error_messages[ERROR_UNEXPECTED_CHAR] = "Unexpected character '%c'";
	error_messages[ERROR_UNTERMINATED_STRING] = "Unterminated string";
	error_messages[ERROR_INVALID_ESCAPE_CHAR] = "Invalid escape character '%c'";
	error_messages[ERROR_INTERPOLATION_NESTING_TOO_DEEP] = "Interpolation nesting is too deep, maximum is %i";
	error_messages[ERROR_NUMBER_IS_TOO_BIG] = "Number is too big to be represented by a single literal";
	error_messages[ERROR_CHAR_EXPECTATION_UNMET] = "Expected '%c' after '%c', got '%c'";

	// Parser errors
	error_messages[ERROR_EXPECTION_UNMET] = "Expected %s";
	error_messages[ERROR_INVALID_ASSIGMENT_TARGET] = "Invalid assigment target";
	error_messages[ERROR_TOO_MANY_FUNCTION_ARGS] = "Function can't have more than 255 arguments, got %i";
	error_messages[ERROR_MULTIPLE_ELSE_BRANCHES] = "If-statement can have only one else-branch";
	error_messages[ERROR_VAR_MISSING_IN_FORIN] = "For-loops using in-iteration must declare a new variable";
	error_messages[ERROR_NO_GETTER_AND_SETTER] = "Expected declaration of either getter or setter, got none";
	error_messages[ERROR_STATIC_OPERATOR] = "Operator methods can't be static or defined in static classes";
	error_messages[ERROR_SELF_INHERITED_CLASS] = "Class can't inherit itself";
	error_messages[ERROR_STATIC_FIELDS_AFTER_METHODS] = "All static fields must be defined before the methods";
	error_messages[ERROR_MISSING_STATEMENT] = "Expected statement but got nothing";

	// Emitter errors
	error_messages[ERROR_TOO_MANY_CONSTANTS] = "Too many constants for one chunk";
	error_messages[ERROR_TOO_MANY_PRIVATES] = "Too many private locals for one module";
	error_messages[ERROR_VAR_REDEFINED] = "Variable '%.*s' was already declared in this scope";
	error_messages[ERROR_TOO_MANY_LOCALS] = "Too many local variables for one function";
	error_messages[ERROR_TOO_MANY_UPVALUES] = "Too many upvalues for one function";
	error_messages[ERROR_VARIABLE_USED_IN_INIT] = "Variable '%.*s' can't use itself in its initializer";
	error_messages[ERROR_JUMP_TOO_BIG] = "Too much code to jump over";
	error_messages[ERROR_THIS_MISSUSE] = "'this' can't be used %s";
	error_messages[ERROR_SUPER_MISSUSE] = "'super' can't be used %s";
	error_messages[ERROR_UNKNOWN_EXPRESSION] = "Unknown expression with id '%i'";
	error_messages[ERROR_UNKNOWN_STATEMENT] = "Unknown statement with id '%i'";
	error_messages[ERROR_LOOP_JUMP_MISSUSE] = "Can't use '%s' outside of loops";
	error_messages[ERROR_RETURN_FROM_CONSTRUCTOR] = "Can't use 'return' in constructors";
	error_messages[ERROR_STATIC_CONSTRUCTOR] = "Constructors can't be static (at least for now)";
}