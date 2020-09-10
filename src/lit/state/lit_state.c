#include <lit/state/lit_state.h>
#include <lit/debug/lit_debug.h>
#include <lit/scanner/lit_scanner.h>
#include <lit/parser/lit_parser.h>
#include <lit/optimizer/lit_optimizer.h>
#include <lit/emitter/lit_emitter.h>
#include <lit/vm/lit_vm.h>
#include <lit/util/lit_fs.h>
#include <lit/std/lit_core.h>
#include <lit/api/lit_api.h>

#include <stdlib.h>
#include <string.h>

static void default_error(LitState* state, LitErrorType type, const char* message, va_list args) {
	fflush(stdout);
	fprintf(stderr, COLOR_RED);
	vfprintf(stderr, message, args);
	fprintf(stderr, "%s\n", COLOR_RESET);
	fflush(stderr);
}

static void default_printf(const char* message, va_list args) {
	vprintf(message, args);
}

LitState* lit_new_state() {
	LitState* state = (LitState*) malloc(sizeof(LitState));

	state->class_class = NULL;
	state->object_class = NULL;
	state->number_class = NULL;
	state->string_class = NULL;
	state->bool_class = NULL;
	state->function_class = NULL;
	state->fiber_class = NULL;
	state->module_class = NULL;
	state->array_class = NULL;
	state->map_class = NULL;
	state->range_class = NULL;

	state->bytes_allocated = 0;
	state->next_gc = 1024 * 1024;
	state->allow_gc = false;

	state->error_fn = default_error;
	state->print_fn = default_printf;
	state->had_error = false;
	state->roots = NULL;
	state->root_count = 0;
	state->root_capacity = 0;
	state->last_module = NULL;

	state->scanner = (LitScanner*) malloc(sizeof(LitScanner));

	state->parser = (LitParser*) malloc(sizeof(LitParser));
	lit_init_parser(state, (LitParser*) state->parser);

	state->emitter = (LitEmitter*) malloc(sizeof(LitEmitter));
	lit_init_emitter(state, state->emitter);

	state->optimizer = (LitOptimizer*) malloc(sizeof(LitOptimizer));
	lit_init_optimizer(state, state->optimizer);

	state->vm = (LitVm*) malloc(sizeof(LitVm));

	lit_init_vm(state, state->vm);
	lit_init_api(state);
	lit_open_core_library(state);

	return state;
}

int64_t lit_free_state(LitState* state) {
	if (state->roots != NULL) {
		free(state->roots);
		state->roots = NULL;
	}

	lit_free_api(state);
	free(state->scanner);

	lit_free_parser(state->parser);
	free(state->parser);

	lit_free_emitter(state->emitter);
	free(state->emitter);

	free(state->optimizer);

	lit_free_vm(state->vm);
	free(state->vm);

	int64_t amount = state->bytes_allocated;
	free(state);

	return amount;
}

void lit_push_root(LitState* state, LitObject* object) {
	lit_push_value_root(state, OBJECT_VALUE(object));
}

void lit_push_value_root(LitState* state, LitValue value) {
	if (state->root_count + 1 >= state->root_capacity) {
		state->root_capacity = LIT_GROW_CAPACITY(state->root_capacity);
		state->roots = realloc(state->roots, state->root_capacity * sizeof(LitValue));
	}

	state->roots[state->root_count++] = value;
}

LitValue lit_peek_root(LitState* state, uint8_t distance) {
	assert(state->root_count - distance + 1 > 0);
	return state->roots[state->root_count - distance - 1];
}

void lit_pop_root(LitState* state) {
	state->root_count--;
}

void lit_pop_roots(LitState* state, uint8_t amount) {
	state->root_count -= amount;
}


LitClass* lit_get_class_for(LitState* state, LitValue value) {
	if (IS_OBJECT(value)) {
		switch (OBJECT_TYPE(value)) {
			case OBJECT_STRING: return state->string_class;
			case OBJECT_USERDATA: return state->object_class;

			case OBJECT_FIELD:
			case OBJECT_FUNCTION:
			case OBJECT_CLOSURE:
			case OBJECT_NATIVE_FUNCTION:
			case OBJECT_NATIVE_PRIMITIVE:
			case OBJECT_BOUND_METHOD:
			case OBJECT_PRIMITIVE_METHOD:
			case OBJECT_NATIVE_METHOD: {
				return state->function_class;
			}

			case OBJECT_FIBER: return state->fiber_class;
			case OBJECT_MODULE: return state->module_class;
			case OBJECT_UPVALUE: {
				LitUpvalue* upvalue = AS_UPVALUE(value);

				if (upvalue->location == NULL) {
					return lit_get_class_for(state, upvalue->closed);
				}

				return lit_get_class_for(state, *upvalue->location);
			}

			case OBJECT_INSTANCE: return AS_INSTANCE(value)->klass;
			case OBJECT_CLASS: return state->class_class;
			case OBJECT_ARRAY: return state->array_class;
			case OBJECT_MAP: return state->map_class;
			case OBJECT_RANGE: return state->range_class;
		}
	} else if (IS_NUMBER(value)) {
		return state->number_class;
	} else if (IS_BOOL(value)) {
		return state->bool_class;
	}

	return NULL;
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

LitModule* lit_compile_module(LitState* state, LitString* module_name, const char* code) {
	bool allowed_gc = state->allow_gc;

	state->allow_gc = false;
	state->had_error = false;

	LitModule *module = NULL;

	// This is a lbc format
	if ((code[1] << 8 | code[0]) == LIT_BYTECODE_MAGIC_NUMBER) {
		module = lit_load_module(state, code);
	} else {
		LitStatements statements;
		lit_init_stataments(&statements);

		if (lit_parse(state->parser, module_name->chars, code, &statements)) {
			free_statements(state, &statements);
			return NULL;
		}

		lit_optimize(state->optimizer, &statements);
		module = lit_emit(state->emitter, &statements, module_name);
		free_statements(state, &statements);
	}

	state->allow_gc = allowed_gc;
	return state->had_error ? NULL : module;
}

LitModule* lit_get_module(LitState* state, const char* name) {
	LitValue value;

	if (lit_table_get(&state->vm->modules->values, CONST_STRING(state, name), &value)) {
		return AS_MODULE(value);
	}

	return NULL;
}

LitInterpretResult lit_internal_interpret(LitState* state, LitString* module_name, const char* code) {
	LitModule* module = lit_compile_module(state, module_name, code);

	if (module == NULL) {
		return (LitInterpretResult) {INTERPRET_COMPILE_ERROR, NULL_VALUE };
	}

	LitInterpretResult result = lit_interpret_module(state, module);
	LitFiber* fiber = module->main_fiber;

	if (!state->had_error && !fiber->abort && fiber->stack_top != fiber->stack) {
		lit_error(state, RUNTIME_ERROR, "Stack offset was not 0");
	}

	state->last_module = module;
	return result;
}

void lit_patch_file_name(char* file_name) {
	int name_length = strlen(file_name);

	// Check, if our file_name ends with .lit or lbc, and remove it
	if (name_length > 4 && (strcmp(file_name + name_length - 4, ".lit") == 0 || strcmp(file_name + name_length - 4, ".lbc") == 0)) {
		file_name[name_length - 4] = '\0';
	}

	for (int i = 0; i < name_length; i++) {
		char c = file_name[i];

		if (c == '/' || c == '\\') {
			file_name[i] = '.';
		}
	}
}

bool lit_compile_and_save_files(LitState* state, char* files[], uint num_files, const char* output_file) {
	lit_set_optimization_level(OPTIMIZATION_LEVEL_EXTREME);
	LitModule* compiled_modules[num_files];

	for (uint i = 0; i < num_files; i++) {
		char* file_name = files[i];
		const char* source = lit_read_file(file_name);

		if (source == NULL) {
			lit_error(state, COMPILE_ERROR, "Failed to open file '%s'", file_name);
			return false;
		}

		lit_patch_file_name(file_name);

		LitString *module_name = lit_copy_string(state, file_name, strlen(file_name));
		LitModule* module = lit_compile_module(state, module_name, source);

		compiled_modules[i] = module;
		free((void*) source);

		if (module == NULL) {
			return false;
		}
	}

	FILE* file = fopen(output_file, "w+b");

	if (file == NULL) {
		lit_error(state, COMPILE_ERROR, "Failed to open for writing file '%s'", output_file);
		return false;
	}

	lit_write_uint16_t(file, LIT_BYTECODE_MAGIC_NUMBER);
	lit_write_uint8_t(file, LIT_BYTECODE_VERSION);
	lit_write_uint16_t(file, num_files);

	for (uint i = 0; i < num_files; i++) {
		lit_save_module(compiled_modules[i], file);
	}

	lit_write_uint16_t(file, LIT_BYTECODE_END_NUMBER);
	fclose(file);

	return true;
}

LitInterpretResult lit_interpret_file(LitState* state, const char* file, bool dump_only) {
	// We have to use this trick because we modify the file_name string, and if
	// The user provides a string constant, he will get a SEGFAULT
	// And who wants that?

	size_t length = strlen(file) + 1;
	char file_name[length];
	memcpy(&file_name, file, length);

	const char* source = lit_read_file(file_name);

	if (source == NULL) {
		lit_error(state, RUNTIME_ERROR, "Failed to open file '%s'", file_name);
		return INTERPRET_RUNTIME_FAIL;
	}

	lit_patch_file_name(file_name);

	LitInterpretResult result;

	if (dump_only) {
		LitString* module_name = lit_copy_string(state, file_name, strlen(file_name));
		LitModule* module = lit_compile_module(state, module_name, source);

		if (module == NULL) {
			result = INTERPRET_RUNTIME_FAIL;
		} else {
			lit_disassemble_module(module, source);
			result = (LitInterpretResult) {INTERPRET_OK, NULL_VALUE};
		}
	} else {
		result = lit_interpret(state, file_name, source);
	}

	free((void*) source);
	return result;
}

void lit_error(LitState* state, LitErrorType type, const char* message, ...) {
	va_list args;
	va_start(args, message);
	state->error_fn(state, type, message, args);
	va_end(args);

	state->had_error = true;
}

void lit_printf(LitState* state, const char* message, ...) {
	va_list args;
	va_start(args, message);
	state->print_fn(message, args);
	va_end(args);
}