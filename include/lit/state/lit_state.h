#ifndef LIT_STATE_H
#define LIT_STATE_H

#include <lit/lit_common.h>
#include <lit/lit_predefines.h>
#include <lit/lit.h>

#include <stdarg.h>

typedef enum {
	COMPILE_ERROR,
	RUNTIME_ERROR
} LitErrorType;

typedef void (*LitErrorFn)(LitState* state, LitErrorType type, uint line, const char* message, va_list args);
typedef void (*LitPrintFn)(const char* message, va_list args);

typedef struct sLitState {
	int64_t bytes_allocated;
	int64_t next_gc;
	bool allow_gc;

	LitErrorFn errorFn;
	LitPrintFn printFn;

	LitValue roots[LIT_ROOT_MAX];
	uint8_t root_count;

	struct sLitScanner* scanner;
	struct sLitParser* parser;
	struct sLitEmitter* emitter;
	struct sLitVm* vm;

	bool had_error;
} sLitState;

typedef enum {
	INTERPRET_OK,
	INTERPRET_COMPILE_ERROR,
	INTERPRET_RUNTIME_ERROR
} LitInterpretResultType;

LitState* lit_new_state();
int64_t lit_free_state(LitState* state);

void lit_push_root(LitState* state, LitObject* object);
void lit_push_value_root(LitState* state, LitValue value);
LitValue lit_peek_root(LitState* state, uint8_t distance);
void lit_pop_root(LitState* state);
void lit_pop_roots(LitState* state, uint8_t amount);

LitInterpretResult lit_internal_interpret(LitState* state, LitString* module_name, const char* code);
LitInterpretResult lit_interpret(LitState* state, const char* module_name, const char* code);

void lit_error(LitState* state, LitErrorType type, uint line, const char* message, ...);
void lit_printf(LitState* state, const char* message, ...);

#endif