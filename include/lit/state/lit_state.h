#ifndef LIT_STATE_H
#define LIT_STATE_H

#include <lit/lit_common.h>
#include <lit/lit_predefines.h>

#include <stdarg.h>

typedef enum {
	COMPILE_ERROR,
	RUNTIME_ERROR
} LitErrorType;

typedef void (*LitErrorFn)(LitState* state, LitErrorType type, uint line, const char* message, va_list args);
typedef void (*LitPrintFn)(const char* message, va_list args);

typedef struct sLitState {
	int64_t bytes_allocated;

	LitErrorFn errorFn;
	LitPrintFn printFn;

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

LitInterpretResult lit_internal_interpret(LitState* state, LitString* module_name, const char* code);
LitInterpretResult lit_interpret(LitState* state, const char* module_name, const char* code);

void lit_error(LitState* state, LitErrorType type, uint line, const char* message, ...);
void lit_printf(LitState* state, const char* message, ...);

#endif