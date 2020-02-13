#include <lit/state/lit_state.h>
#include <lit/vm/lit_vm.h>
#include <lit/debug/lit_debug.h>
#include <lit/emitter/lit_emitter.h>
#include <lit/parser/lit_parser.h>
#include <lit/scanner/lit_scanner.h>

#include <stdlib.h>
#include <string.h>

static void default_error(LitState* state, LitErrorType type, uint line, const char* message, va_list args) {
	if (line > 0) {
		fprintf(stderr, "[line %d]: ", line);
	}

	vfprintf(stderr, message, args);
	fprintf(stderr, "\n");
}

static void default_printf(const char* message, va_list args) {
	vprintf(message, args);
}

LitState* lit_new_state() {
	LitState* state = (LitState*) malloc(sizeof(LitState));

	state->bytes_allocated = 0;
	state->errorFn = default_error;
	state->printFn = default_printf;
	state->had_error = false;

	state->scanner = (LitScanner*) malloc(sizeof(LitScanner));

	state->parser = (LitParser*) malloc(sizeof(LitParser));
	lit_init_parser(state, (LitParser*) state->parser);

	state->emitter = (LitEmitter*) malloc(sizeof(LitEmitter));
	lit_init_emitter(state, state->emitter);

	state->vm = (LitVm*) malloc(sizeof(LitVm));

	lit_init_vm(state, state->vm);
	lit_define_std(state->vm);

	return state;
}

int64_t lit_free_state(LitState* state) {
	free(state->scanner);

	lit_free_parser(state->parser);
	free(state->parser);

	lit_free_emitter(state->emitter);
	free(state->emitter);

	lit_free_vm(state->vm);
	free(state->vm);

	int64_t amount = state->bytes_allocated;
	free(state);

	return amount;
}

static void free_statements(LitState* state, LitStatements* statements) {
	for (uint i = 0; i < statements->count; i++) {
		lit_free_statement(state, statements->values[i]);
	}

	lit_free_stataments(state, statements);
}

LitInterpretResult lit_interpret(LitState* state, const char* module_name, const char* code) {
	return lit_internal_interpret(state, lit_copy_string(state, module_name, strlen(module_name)), code);
}

LitInterpretResult lit_internal_interpret(LitState* state, LitString* module_name, const char* code) {
	state->had_error = false;

	LitStatements statements;
	lit_init_stataments(&statements);

	if (lit_parse(state->parser, module_name->chars, code, &statements)) {
		free_statements(state, &statements);
		return (LitInterpretResult) {INTERPRET_COMPILE_ERROR, NULL_VALUE };
	}

	LitFunction* function = lit_emit(state->emitter, &statements);
	free_statements(state, &statements);

	LitInterpretResult result;

	if (state->had_error) {
		result = (LitInterpretResult) {INTERPRET_COMPILE_ERROR, NULL_VALUE };
	} else {
		LitModule* module = lit_create_module(state, module_name);
		lit_table_set(state, &state->vm->modules, module_name, OBJECT_VALUE(module));

		result = lit_interpret_function(state, module, function);
		module->return_value = result.result;

		if (state->vm->fiber->stack_top != state->vm->fiber->stack) {
			lit_error(state, RUNTIME_ERROR, 0, "Stack had left over trash in it");
		}

		state->vm->fiber = state->vm->fiber->parent;
	}

	return result;
}

void lit_error(LitState* state, LitErrorType type, uint line, const char* message, ...) {
	va_list args;
	va_start(args, message);
	state->errorFn(state, type, line, message, args);
	va_end(args);

	state->had_error = true;
}

void lit_printf(LitState* state, const char* message, ...) {
	va_list args;
	va_start(args, message);
	state->printFn(message, args);
	va_end(args);
}