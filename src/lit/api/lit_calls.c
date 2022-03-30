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
	if (IS_STRING(object)) {
		return AS_STRING(object);
	} else if (!IS_OBJECT(object)) {
		if (IS_NULL(object)) {
			return CONST_STRING(state, "null");
		} else if (IS_NUMBER(object)) {
			return AS_STRING(lit_number_to_string(state, AS_NUMBER(object)));
		} else if (IS_BOOL(object)) {
			return CONST_STRING(state, AS_BOOL(object) ? "true" : "false");
		}
	} else if (IS_REFERENCE(object)) {
		LitValue* slot = AS_REFERENCE(object)->slot;

		if (slot == NULL) {
			return CONST_STRING(state, "null");
		}

		return lit_to_string(state, *slot);
	}

	LitVm* vm = state->vm;
	LitFiber* fiber = vm->fiber;

	if (ensure_fiber(vm, fiber)) {
		return CONST_STRING(state, "null");
	}

	LitFunction* function = state->api_function;

	if (function == NULL) {
		function = state->api_function = lit_create_function(state, fiber->module);
		function->chunk.has_line_info = false;
		function->name = state->api_name;

		LitChunk* chunk = &function->chunk;
		chunk->count = 0;
		chunk->constants.count = 0;
		function->max_registers = 2;

		int constant = lit_chunk_add_constant(state, chunk, OBJECT_CONST_STRING(state, "toString"));

		lit_write_chunk(state, chunk, LIT_FORM_ABC_INSTRUCTION(OP_INVOKE, 1, 1, constant), 1);
		lit_write_chunk(state, chunk, LIT_FORM_ABC_INSTRUCTION(OP_RETURN, 1, 0, 0), 1);
	}

	lit_ensure_fiber_registers(state, fiber, function->max_registers + fiber->registers_used);
	LitCallFrame* frame = &fiber->frames[fiber->frame_count++];

	frame->ip = function->chunk.code;
	frame->closure = NULL;
	frame->function = function;
	frame->slots = fiber->registers + fiber->registers_used;
	frame->result_ignored = false;
	frame->return_address = NULL;

	frame->slots[0] = OBJECT_VALUE(function);
	frame->slots[1] = object;

	LitInterpretResult result = lit_interpret_fiber(state, fiber);

	if (result.type != INTERPRET_OK) {
		return CONST_STRING(state, "null");
	}

	return AS_STRING(result.result);
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