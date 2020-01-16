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
	uint bytes_allocated;

	LitErrorFn errorFn;
	LitPrintFn printFn;

	struct sLitScanner* scanner;
	struct sLitParser* parser;
	struct sLitEmitter* emitter;
	struct sLitVm* vm;
} sLitState;

typedef enum {
	INTERPRET_OK,
	INTERPRET_COMPILE_ERROR,
	INTERPRET_RUNTIME_ERROR
} LitInterpretResult;

LitState* lit_new_state();
uint lit_free_state(LitState* state);
LitInterpretResult lit_interpret(LitState* state, char* code);

void lit_error(LitState* state, LitErrorType type, uint line, const char* message, ...);
void lit_printf(LitState* state, const char* message, ...);

#endif