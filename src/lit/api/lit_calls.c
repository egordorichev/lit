#include "api/lit_calls.h"
#include "api/lit_api.h"
#include "debug/lit_debug.h"

#include <math.h>

#define PUSH(value) (*fiber->stack_top++ = value)

static bool ensure_fiber(LitVm* vm, LitFiber* fiber) {
	if (fiber == NULL) {
		lit_runtime_error(vm, "No fiber to run on");
		return true;
	}

	if (fiber->frame_count == LIT_CALL_FRAMES_MAX) {
		lit_runtime_error(vm, "Stack overflow");
		return true;
	}

	if (fiber->frame_count + 1 > fiber->frame_capacity) {
		uint new_capacity = fmin(LIT_CALL_FRAMES_MAX, fiber->frame_capacity * 2);

		fiber->frames = (LitCallFrame*) lit_reallocate(vm->state, fiber->frames, sizeof(LitCallFrame) * fiber->frame_capacity, sizeof(LitCallFrame) * new_capacity);
		fiber->frame_capacity = new_capacity;
	}

	return false;
}

static inline LitCallFrame* setup_call(LitState* state, LitFunction* callee, LitValue* arguments, uint8_t argument_count) {
	NOT_IMPLEMENTED
	return NULL;
}

static inline LitInterpretResult execute_call(LitState* state, LitCallFrame* frame) {
	if (frame == NULL) {
		RETURN_RUNTIME_ERROR()
	}

	LitFiber* fiber = state->vm->fiber;
	LitInterpretResult result = lit_interpret_fiber(state, fiber);

	if (fiber->error != NULL_VALUE) {
		result.result = fiber->error;
	}

	return result;
}

LitInterpretResult lit_call_function(LitState* state, LitFunction* callee, LitValue* arguments, uint8_t argument_count) {
	return execute_call(state, setup_call(state, callee, arguments, argument_count));
}

LitInterpretResult lit_call_closure(LitState* state, LitClosure* callee, LitValue* arguments, uint8_t argument_count) {
	LitCallFrame* frame = setup_call(state, callee->function, arguments, argument_count);

	if (frame == NULL) {
		RETURN_RUNTIME_ERROR()
	}

	frame->closure = callee;
	return execute_call(state, frame);
}

LitInterpretResult lit_call_method(LitState* state, LitValue instance, LitValue callee, LitValue* arguments, uint8_t argument_count) {
	LitVm* vm = state->vm;
	NOT_IMPLEMENTED

	if (IS_NULL(callee)) {
		lit_runtime_error(vm, "Attempt to call a null value");
	} else {
		lit_runtime_error(vm, "Can only call functions and classes");
	}

	RETURN_RUNTIME_ERROR()
}

LitInterpretResult lit_call(LitState* state, LitValue callee, LitValue* arguments, uint8_t argument_count) {
	return lit_call_method(state, callee, callee, arguments, argument_count);
}

LitInterpretResult lit_find_and_call_method(LitState* state, LitValue callee, LitString* method_name, LitValue* arguments, uint8_t argument_count) {
	LitVm* vm = state->vm;
	LitFiber* fiber = vm->fiber;

	if (fiber == NULL) {
		lit_runtime_error(vm, "No fiber to run on");
		RETURN_RUNTIME_ERROR()
	}

	LitClass* klass = lit_get_class_for(state, callee);
	LitValue method;

	if ((IS_INSTANCE(callee) && lit_table_get(&AS_INSTANCE(callee)->fields, method_name, &method)) || lit_table_get(&klass->methods, method_name, &method)) {
		return lit_call_method(state, callee, method, arguments, argument_count);
	}

	return (LitInterpretResult) { INTERPRET_INVALID, NULL_VALUE };
}

LitString* lit_to_string(LitState* state, LitValue object) {
	NOT_IMPLEMENTED
	return NULL;
}

LitValue lit_call_new(LitVm* vm, const char* name, LitValue* args, uint argument_count) {
	LitValue value;

	if (!lit_table_get(&vm->globals->values, CONST_STRING(vm->state, name), &value)) {
		lit_runtime_error(vm, "Failed to create instance of class %s: class not found", name);
		return NULL_VALUE;
	}

	LitClass* klass = AS_CLASS(value);

	if (klass->init_method == NULL) {
		return OBJECT_VALUE(lit_create_instance(vm->state, klass));
	}

	return lit_call_method(vm->state, value, value, args, argument_count).result;
}

#undef PUSH