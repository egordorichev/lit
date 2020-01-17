#ifndef LIT_VM_H
#define LIT_VM_H

#include <lit/vm/lit_chunk.h>
#include <lit/state/lit_state.h>
#include <lit/lit_predefines.h>

#include <lit/lit.h>

typedef struct sLitVm {
	LitState* state;

	LitValue stack[LIT_STACK_MAX];
	LitValue* stack_top;

	LitChunk* chunk;
	uint8_t* ip;

	LitObject* objects;
} sLitVm;

void lit_init_vm(LitState* state, LitVm* vm);
void lit_free_vm(LitVm* vm);

void lit_push(LitVm* vm, LitValue value);
LitValue lit_pop(LitVm* vm);

LitInterpretResult lit_interpret_chunk(LitState* state, LitChunk* chunk);

#endif