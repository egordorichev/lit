#ifndef LIT_VM_H
#define LIT_VM_H

#include "vm/lit_chunk.h"
#include "vm/lit_object.h"
#include "state/lit_state.h"
#include "lit_predefines.h"
#include "util/lit_table.h"

#include "lit_config.h"

#define INTERPRET_RUNTIME_FAIL ((LitInterpretResult) {INTERPRET_INVALID, NULL_VALUE})

typedef struct sLitVm {
	LitState* state;

	LitObject* objects;

	LitTable strings;

	LitMap* modules;
	LitMap* globals;

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
bool lit_vruntime_error(LitVm* vm, const char* format, va_list args);
bool lit_runtime_error(LitVm* vm, const char* format, ...);
bool lit_runtime_error_exiting(LitVm* vm, const char* format, ...);

void lit_native_exit_jump();

#endif