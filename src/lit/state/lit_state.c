#include "state/lit_state.h"
#include"debug/lit_debug.h"
#include "scanner/lit_scanner.h"
#include "parser/lit_parser.h"
#include "optimizer/lit_optimizer.h"
#include "preprocessor/lit_preprocessor.h"
#include "emitter/lit_emitter.h"
#include "vm/lit_vm.h"
#include "util/lit_fs.h"
#include "std/lit_core.h"
#include "api/lit_api.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

static bool measure_compilation_time;
static double last_source_time = 0;

void lit_enable_compilation_time_measurement() {
	measure_compilation_time = true;
}

static void default_error(LitState* state, const char* message) {
	fflush(stdout);
	fprintf(stderr, "%s%s%s\n", COLOR_RED, message, COLOR_RESET);
	fflush(stderr);
}

static void default_printf(LitState* state, const char* message) {
	printf("%s", message);
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

	state->next_gc = 256 * 1024;
	state->allow_gc = false;

	state->error_fn = default_error;
	state->print_fn = default_printf;
	state->had_error = false;
	state->roots = NULL;
	state->root_count = 0;
	state->root_capacity = 0;
	state->last_module = NULL;

	state->preprocessor = (LitPreprocessor*) malloc(sizeof(LitPreprocessor));
	lit_init_preprocessor(state, state->preprocessor);

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

	lit_free_preprocessor(state->preprocessor);
	free(state->preprocessor);

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

			case OBJECT_REFERENCE: {
				LitValue* slot = AS_REFERENCE(value)->slot;

				if (slot != NULL) {
					return lit_get_class_for(state, *slot);
				}

				return state->object_class;
			}
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

LitInterpretResult lit_interpret(LitState* state, const char* module_name, char* code) {
	return lit_internal_interpret(state, lit_copy_string(state, module_name, strlen(module_name)), code);
}

LitModule* lit_compile_module(LitState* state, LitString* module_name, char* code) {
	bool allowed_gc = state->allow_gc;

	state->allow_gc = false;
	state->had_error = false;

	LitModule *module = NULL;

	// This is a lbc format
	if ((code[1] << 8 | code[0]) == LIT_BYTECODE_MAGIC_NUMBER) {
		module = lit_load_module(state, code);
	} else {
		clock_t t = 0;
		clock_t total_t = 0;

		if (measure_compilation_time) {
			total_t = t = clock();
		}

		if (!lit_preprocess(state->preprocessor, code)) {
			return NULL;
		}

		if (measure_compilation_time) {
			printf("-----------------------\nPreprocessing:  %gms\n", (double) (clock() - t) / CLOCKS_PER_SEC * 1000);
			t = clock();
		}

		LitStatements statements;
		lit_init_stataments(&statements);

		if (lit_parse(state->parser, module_name->chars, code, &statements)) {
			free_statements(state, &statements);
			return NULL;
		}

		if (measure_compilation_time) {
			printf("Parsing:        %gms\n", (double) (clock() - t) / CLOCKS_PER_SEC * 1000);
			t = clock();
		}

		lit_optimize(state->optimizer, &statements);

		if (measure_compilation_time) {
			printf("Optimization:   %gms\n", (double) (clock() - t) / CLOCKS_PER_SEC * 1000);
			t = clock();
		}

		module = lit_emit(state->emitter, &statements, module_name);
		free_statements(state, &statements);

		if (measure_compilation_time) {
			printf("Emitting:       %gms\n", (double) (clock() - t) / CLOCKS_PER_SEC * 1000);
			printf("\nTotal:          %gms\n-----------------------\n", (double) (clock() - total_t) / CLOCKS_PER_SEC * 1000 + last_source_time);
		}
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

LitInterpretResult lit_internal_interpret(LitState* state, LitString* module_name, char* code) {
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

char* lit_patch_file_name(char* file_name) {
	int name_length = strlen(file_name);

	// Check, if our file_name ends with .lit or lbc, and remove it
	if (name_length > 4 && (memcmp(file_name + name_length - 4, ".lit", 4) == 0 || memcmp(file_name + name_length - 4, ".lbc", 4) == 0)) {
		file_name[name_length - 4] = '\0';
		name_length -= 4;
	}

	// Check, if our file_name starts with ./ and remove it (useless, and makes the module name be ..main)
	if (name_length > 2 && memcmp(file_name, "./", 2) == 0) {
		file_name += 2;
		name_length -= 2;
	}

	for (int i = 0; i < name_length; i++) {
		char c = file_name[i];

		if (c == '/' || c == '\\') {
			file_name[i] = '.';
		}
	}

	return file_name;
}

char* copy_string(const char* string) {
	size_t length = strlen(string) + 1;
	char* new_string = malloc(length);
	memcpy(new_string, string, length);

	return new_string;
}

bool lit_compile_and_save_files(LitState* state, char* files[], uint num_files, const char* output_file) {
	lit_set_optimization_level(OPTIMIZATION_LEVEL_EXTREME);
	LitModule* compiled_modules[num_files];

	for (uint i = 0; i < num_files; i++) {
		char* file_name = copy_string(files[i]);
		char* source = lit_read_file(file_name);

		if (source == NULL) {
			lit_error(state, COMPILE_ERROR, "Failed to open file '%s'", file_name);
			return false;
		}

		file_name = lit_patch_file_name(file_name);

		LitString *module_name = lit_copy_string(state, file_name, strlen(file_name));
		LitModule* module = lit_compile_module(state, module_name, source);

		compiled_modules[i] = module;

		free((void*) source);
		free((void*) file_name);

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

static char* read_source(LitState* state, const char* file, char** patched_file_name) {
	clock_t t = 0;

	if (measure_compilation_time) {
		t = clock();
	}

	char* file_name = copy_string(file);
	char* source = lit_read_file(file_name);

	if (source == NULL) {
		lit_error(state, RUNTIME_ERROR, "Failed to open file '%s'", file_name);
	}

	file_name = lit_patch_file_name(file_name);

	if (measure_compilation_time) {
		printf("Reading source: %gms\n", last_source_time = (double) (clock() - t) / CLOCKS_PER_SEC * 1000);
	}

	*patched_file_name = file_name;
	return source;
}

LitInterpretResult lit_interpret_file(LitState* state, const char* file) {
	char* patched_file_name;
	char* source = read_source(state, file, &patched_file_name);

	if (source == NULL) {
		return INTERPRET_RUNTIME_FAIL;
	}

	LitInterpretResult result = lit_interpret(state, patched_file_name, source);

	free((void*) source);
	return result;
}

LitInterpretResult lit_dump_file(LitState* state, const char* file) {
	char* patched_file_name;
	char* source = read_source(state, file, &patched_file_name);

	if (source == NULL) {
		return INTERPRET_RUNTIME_FAIL;
	}

	LitInterpretResult result;
	LitString* module_name = lit_copy_string(state, patched_file_name, strlen(patched_file_name));
	LitModule* module = lit_compile_module(state, module_name, source);

	if (module == NULL) {
		result = INTERPRET_RUNTIME_FAIL;
	} else {
		lit_disassemble_module(module, source);
		result = (LitInterpretResult) {INTERPRET_OK, NULL_VALUE};
	}

	free((void*) source);
	free((void*) patched_file_name);

	return result;
}

void lit_error(LitState* state, LitErrorType type, const char* message, ...) {
	va_list args;
	va_start(args, message);
	va_list args_copy;
	va_copy(args_copy, args);
	size_t buffer_size = vsnprintf(NULL, 0, message, args_copy) + 1;
	va_end(args_copy);

	char buffer[buffer_size];
	vsnprintf(buffer, buffer_size, message, args);
	va_end(args);

	state->error_fn(state, buffer);
	state->had_error = true;
}

void lit_printf(LitState* state, const char* message, ...) {
	va_list args;
	va_start(args, message);
	va_list args_copy;
	va_copy(args_copy, args);
	size_t buffer_size = vsnprintf(NULL, 0, message, args_copy) + 1;
	va_end(args_copy);

	char buffer[buffer_size];
	vsnprintf(buffer, buffer_size, message, args);
	va_end(args);

	state->print_fn(state, buffer);
}