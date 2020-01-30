#include <lit/state/lit_state.h>
#include <lit/vm/lit_vm.h>
#include <lit/mem/lit_mem.h>
#include <lit/debug/lit_debug.h>
#include <lit/emitter/lit_emitter.h>
#include <lit/parser/lit_parser.h>
#include <lit/scanner/lit_scanner.h>

#include <stdlib.h>

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

LitInterpretResult lit_interpret(LitState* state, char* code) {
	state->had_error = false;

	LitStatements statements;
	lit_init_stataments(&statements);

	if (lit_parse(state->parser, code, &statements)) {
		free_statements(state, &statements);
		return INTERPRET_COMPILE_ERROR;
	}

	LitChunk* chunk = lit_emit(state->emitter, &statements);
	free_statements(state, &statements);

	LitInterpretResult result;

	if (state->had_error) {
		result = INTERPRET_COMPILE_ERROR;
	} else {
		result = lit_interpret_chunk(state, chunk);
	}

	lit_free_chunk(state, chunk);

	// TMP line, because chunk is allocated by hand for now, we need to free it:
	lit_reallocate(state, chunk, sizeof(LitChunk), 0);

	if (state->vm->stack_top != state->vm->stack) {
		lit_error(state, RUNTIME_ERROR, 0, "Stack had left over trash in it");
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