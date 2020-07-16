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
	LitUpvalue* open_upvalues;

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

void lit_push(LitVm* vm, LitValue value);
LitValue lit_pop(LitVm* vm);

LitInterpretResult lit_interpret_module(LitState* state, LitModule* module);
LitInterpretResult lit_interpret_fiber(LitState* state, LitFiber* fiber);
void lit_handle_runtime_error(LitVm* vm, LitString* error_string);
void lit_runtime_error(LitVm* vm, const char* format, ...);

#endif