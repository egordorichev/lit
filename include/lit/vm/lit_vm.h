#ifndef LIT_VM_H
#define LIT_VM_H

#include <lit/vm/lit_chunk.h>
#include <lit/vm/lit_object.h>
#include <lit/state/lit_state.h>
#include <lit/lit_predefines.h>
#include <lit/util/lit_table.h>

#include <lit/lit_config.h>

#define INTERPRET_RUNTIME_FAIL ((LitInterpretResult) {INTERPRET_RUNTIME_ERROR, NULL_VALUE})

typedef struct sLitVm {
	LitState* state;

	LitObject* objects;

	LitTable strings;
	LitTable globals;
	LitTable modules;

	LitFiber* fiber;

	// For garbage collection
	uint gray_count;
	uint gray_capacity;
	LitObject** gray_stack;
} sLitVm;

typedef struct sLitInterpretResult {
	LitInterpretResultType type;
	LitValue result;
} LitInterpretResult;

void lit_init_vm(LitState* state, LitVm* vm);
void lit_free_vm(LitVm* vm);

static inline void lit_push(LitVm* vm, LitValue value) {
	*vm->fiber->stack_top++ = value;
}

static inline LitValue lit_pop(LitVm* vm) {
	return *(--vm->fiber->stack_top);
}

LitInterpretResult lit_interpret_module(LitState* state, LitModule* module);
LitInterpretResult lit_interpret_fiber(LitState* state, LitFiber* fiber);
bool lit_handle_runtime_error(LitVm* vm, LitString* error_string);
bool lit_runtime_error(LitVm* vm, const char* format, ...);

#endif