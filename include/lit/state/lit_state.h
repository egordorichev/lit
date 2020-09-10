#ifndef LIT_STATE_H
#define LIT_STATE_H

#include <lit/lit_common.h>
#include <lit/lit_predefines.h>
#include <lit/vm/lit_object.h>
#include <lit/lit_config.h>

#include <stdarg.h>

typedef enum {
	COMPILE_ERROR,
	RUNTIME_ERROR
} LitErrorType;

typedef void (*LitErrorFn)(LitState* state, LitErrorType type, const char* message, va_list args);
typedef void (*LitPrintFn)(const char* message, va_list args);

typedef struct sLitState {
	int64_t bytes_allocated;
	int64_t next_gc;
	bool allow_gc;

	LitErrorFn error_fn;
	LitPrintFn print_fn;

	LitValue* roots;
	uint root_count;
	uint root_capacity;

	struct sLitScanner* scanner;
	struct sLitParser* parser;
	struct sLitEmitter* emitter;
	struct sLitOptimizer* optimizer;
	struct sLitVm* vm;

	bool had_error;

	LitFunction* api_function;
	LitFunction* api_string_function;
	LitFiber* api_fiber;
	LitString* api_name;

	// Mental note:
	// When adding another class here, DO NOT forget to mark it in lit_mem.c or it will be GC-ed
	LitClass* class_class;
	LitClass* object_class;
	LitClass* number_class;
	LitClass* string_class;
	LitClass* bool_class;
	LitClass* function_class;
	LitClass* fiber_class;
	LitClass* module_class;
	LitClass* array_class;
	LitClass* map_class;
	LitClass* range_class;

	LitModule* last_module;
} sLitState;

typedef enum {
	INTERPRET_OK,
	INTERPRET_COMPILE_ERROR,
	INTERPRET_RUNTIME_ERROR,
	INTERPRET_INVALID
} LitInterpretResultType;

LitState* lit_new_state();
int64_t lit_free_state(LitState* state);

void lit_push_root(LitState* state, LitObject* object);
void lit_push_value_root(LitState* state, LitValue value);
LitValue lit_peek_root(LitState* state, uint8_t distance);
void lit_pop_root(LitState* state);
void lit_pop_roots(LitState* state, uint8_t amount);

LitClass* lit_get_class_for(LitState* state, LitValue value);

void lit_patch_file_name(char* file_name);

LitModule* lit_compile_module(LitState* state, LitString* module_name, const char* code);
LitModule* lit_get_module(LitState* state, const char* name);

LitInterpretResult lit_internal_interpret(LitState* state, LitString* module_name, const char* code);
LitInterpretResult lit_interpret(LitState* state, const char* module_name, const char* code);
LitInterpretResult lit_interpret_file(LitState* state, const char* file, bool dump_only);
bool lit_compile_and_save_files(LitState* state, char* files[], uint num_files, const char* output_file);

void lit_error(LitState* state, LitErrorType type, const char* message, ...);
void lit_printf(LitState* state, const char* message, ...);

#endif