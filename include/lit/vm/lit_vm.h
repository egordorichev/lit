#ifndef LIT_VM_H
#define LIT_VM_H

#include <lit/vm/lit_chunk.h>
#include <lit/vm/lit_object.h>
#include <lit/state/lit_state.h>
#include <lit/lit_predefines.h>
#include <lit/util/lit_table.h>

#include <lit/lit.h>

typedef struct sLitVm {
	LitState* state;

	LitObject* objects;

	LitTable strings;
	LitTable globals;
	LitTable modules;

	LitFiber* fiber;

	LitObject* roots[LIT_ROOT_MAX];
	uint8_t root_count;
} sLitVm;

typedef struct sLitInterpretResult {
	LitInterpretResultType type;
	LitValue result;
} LitInterpretResult;

void lit_init_vm(LitState* state, LitVm* vm);
void lit_define_std(LitVm* vm);
void lit_free_vm(LitVm* vm);

void lit_push(LitVm* vm, LitValue value);
LitValue lit_pop(LitVm* vm);

LitInterpretResult lit_interpret_module(LitState* state, LitModule* module);
LitInterpretResult lit_interpret_fiber(LitState* state, LitFiber* fiber);

#endif