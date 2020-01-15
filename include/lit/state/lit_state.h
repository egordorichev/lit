#ifndef LIT_STATE_H
#define LIT_STATE_H

#include <lit/lit_common.h>
#include <lit/lit_predefines.h>

typedef struct sLitState {
	uint bytes_allocated;

	struct LitScanner* scanner;
	struct LitParser* parser;
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

#endif