#ifndef LIT_VM_H
#define LIT_VM_H

#include "lit/vm/lit_chunk.h"
#include "lit/vm/lit_object.h"
#include "lit/state/lit_state.h"
#include "lit/lit_predefines.h"
#include "lit/util/lit_table.h"
#include "lit/lit_config.h"

#include <setjmp.h>

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

LitInterpretResult lit_interpret_module(LitState* state, LitModule* module);

LitInterpretResult lit_interpret_fiber(LitState* state, LitFiber* fiber);
bool lit_handle_runtime_error(LitVm* vm, LitString* error_string);
bool lit_vruntime_error(LitVm* vm, const char* format, va_list args);
bool lit_runtime_error(LitVm* vm, const char* format, ...);
bool lit_runtime_error_exiting(LitVm* vm, const char* format, ...);

extern jmp_buf jump_buffer;
void lit_native_exit_jump();
#define lit_set_native_exit_jump() setjmp(jump_buffer)

#endif