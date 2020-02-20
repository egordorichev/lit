#include <lit/state/lit_state.h>
#include <lit/vm/lit_vm.h>
#include <lit/debug/lit_debug.h>
#include <lit/emitter/lit_emitter.h>
#include <lit/parser/lit_parser.h>
#include <lit/scanner/lit_scanner.h>
#include <lit/util/lit_fs.h>

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
	state->next_gc = 1024 * 1024;
	state->allow_gc = false;

	state->errorFn = default_error;
	state->printFn = default_printf;
	state->had_error = false;
	state->root_count = 0;

	state->api_fiber = NULL;
	state->api_function = NULL;
	state->api_module = NULL;

	state->scanner = (LitScanner*) malloc(sizeof(LitScanner));

	state->parser = (LitParser*) malloc(sizeof(LitParser));
	lit_init_parser(state, (LitParser*) state->parser);

	state->emitter = (LitEmitter*) malloc(sizeof(LitEmitter));
	lit_init_emitter(state, state->emitter);

	state->vm = (LitVm*) malloc(sizeof(LitVm));

	lit_init_vm(state, state->vm);
	lit_define_std(state);

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

void lit_push_root(LitState* state, LitObject* object) {
	assert(state->root_count < LIT_ROOT_MAX);
	state->roots[state->root_count++] = OBJECT_VALUE(object);
}

void lit_push_value_root(LitState* state, LitValue value) {
	assert(state->root_count < LIT_ROOT_MAX);
	state->roots[state->root_count++] = value;
}

LitValue lit_peek_root(LitState* state, uint8_t distance) {
	assert(state->root_count - distance + 1 > 0);
	return state->roots[state->root_count - distance - 1];
}

void lit_pop_root(LitState* state) {
	assert(state->root_count > 0);
	state->root_count--;
}

void lit_pop_roots(LitState* state, uint8_t amount) {
	assert(state->root_count - amount >= 0);
	state->root_count -= amount;
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
	bool allowed_gc = state->allow_gc;

	state->allow_gc = false;
	state->had_error = false;

	LitStatements statements;
	lit_init_stataments(&statements);

	if (lit_parse(state->parser, module_name->chars, code, &statements)) {
		free_statements(state, &statements);
		return (LitInterpretResult) {INTERPRET_COMPILE_ERROR, NULL_VALUE };
	}

	LitModule* module = lit_emit(state->emitter, &statements, module_name);
	free_statements(state, &statements);

	LitInterpretResult result;

	if (state->had_error) {
		result = (LitInterpretResult) {INTERPRET_COMPILE_ERROR, NULL_VALUE };
		state->allow_gc = allowed_gc;
	} else {
		state->allow_gc = true;
		result = lit_interpret_module(state, module);
		state->allow_gc = allowed_gc;
		module->return_value = result.result;

		if (state->vm->fiber->stack_top != state->vm->fiber->stack) {
			lit_error(state, RUNTIME_ERROR, 0, "Stack had left over trash in it");
		}

		state->vm->fiber = state->vm->fiber->parent;
	}

	return result;
}

LitInterpretResult lit_interpret_file(LitState* state, const char* file_name) {
	const char* source = lit_read_file(file_name);

	if (source == NULL) {
		lit_error(state, RUNTIME_ERROR, 0, "Failed top open file '%s'", file_name);
		return INTERPRET_RUNTIME_FAIL;
	}

	LitInterpretResult result = lit_interpret(state, file_name, source);
	free((void*) source);

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