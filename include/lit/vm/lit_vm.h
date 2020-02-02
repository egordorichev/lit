#ifndef LIT_VM_H
#define LIT_VM_H

#include <lit/vm/lit_chunk.h>
#include <lit/vm/lit_object.h>
#include <lit/state/lit_state.h>
#include <lit/lit_predefines.h>
#include <lit/util/lit_table.h>

#include <lit/lit.h>

typedef struct {
	LitFunction* function;
	uint8_t* ip;
	LitValue* slots;
} LitCallFrame;

typedef struct sLitVm {
	LitState* state;

	LitValue stack[LIT_STACK_MAX];
	LitValue* stack_top;

	LitCallFrame frames[LIT_CALL_FRAMES_MAX];
	uint frame_count;

	LitObject* objects;
	LitTable strings;
	LitTable globals;
} sLitVm;

void lit_init_vm(LitState* state, LitVm* vm);
void lit_free_vm(LitVm* vm);

void lit_push(LitVm* vm, LitValue value);
LitValue lit_pop(LitVm* vm);

LitInterpretResult lit_interpret_function(LitState* state, LitFunction* function);
LitInterpretResult lit_interpret_frame(LitState* state);

#endif