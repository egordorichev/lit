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
	LitVm* vm = state->vm;
	LitFiber* fiber = vm->fiber;

	if (ensure_fiber(vm, fiber)) {
		return NULL;
	}

	lit_ensure_fiber_stack(state, fiber, callee->max_slots + (int) (fiber->stack_top - fiber->stack));

	LitCallFrame* frame = &fiber->frames[fiber->frame_count++];
	frame->slots = fiber->stack_top;

	PUSH(OBJECT_VALUE(callee));

	for (uint8_t i = 0; i < argument_count; i++) {
		PUSH(arguments[i]);
	}

	uint function_arg_count = callee->arg_count;

	if (argument_count != function_arg_count) {
		bool vararg = callee->vararg;

		if (argument_count < function_arg_count) {
			int amount = (int) function_arg_count - argument_count - (vararg ? 1 : 0);

			for (int i = 0; i < amount; i++) {
				PUSH(NULL_VALUE);
			}

			if (vararg) {
				PUSH(OBJECT_VALUE(lit_create_array(vm->state)));
			}
		} else if (callee->vararg) {
			LitArray* array = lit_create_array(vm->state);
			uint vararg_count = argument_count - function_arg_count + 1;

			lit_values_ensure_size(vm->state, &array->values, vararg_count);

			for (uint i = 0; i < vararg_count; i++) {
				array->values.values[i] = fiber->stack_top[(int) i - (int) vararg_count];
			}

			fiber->stack_top -= vararg_count;
			lit_push(vm, OBJECT_VALUE(array));
		} else {
			fiber->stack_top -= (argument_count - function_arg_count);
		}
	} else if (callee->vararg) {
		LitArray* array = lit_create_array(vm->state);
		uint vararg_count = argument_count - function_arg_count + 1;

		lit_values_write(vm->state, &array->values, *(fiber->stack_top - 1));
		*(fiber->stack_top - 1) = OBJECT_VALUE(array);
	}

	frame->ip = callee->chunk.code;
	frame->closure = NULL;
	frame->function = callee;
	frame->result_ignored = false;
	frame->return_to_c = true;

	return frame;
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
	
	if (IS_OBJECT(callee)) {
		if (lit_set_native_exit_jump()) {
			RETURN_RUNTIME_ERROR()
		}

		LitObjectType type = OBJECT_TYPE(callee);

		if (type == OBJECT_FUNCTION) {
			return lit_call_function(state, AS_FUNCTION(callee), arguments, argument_count);
		} else if (type == OBJECT_CLOSURE) {
			return lit_call_closure(state, AS_CLOSURE(callee), arguments, argument_count);
		}

		LitFiber* fiber = vm->fiber;

		if (ensure_fiber(vm, fiber)) {
			RETURN_RUNTIME_ERROR()
		}

		lit_ensure_fiber_stack(state, fiber, 3 + argument_count + (int) (fiber->stack_top - fiber->stack));
		LitValue* slot = fiber->stack_top;

		PUSH(instance);

		if (type != OBJECT_CLASS) {
			for (uint8_t i = 0; i < argument_count; i++) {
				PUSH(arguments[i]);
			}
		}

		switch (type) {
			case OBJECT_NATIVE_FUNCTION: {
				LitValue result = AS_NATIVE_FUNCTION(callee)->function(vm, argument_count, fiber->stack_top - argument_count);
				fiber->stack_top = slot;

				RETURN_OK(result)
			}

			case OBJECT_NATIVE_PRIMITIVE: {
				AS_NATIVE_PRIMITIVE(callee)->function(vm, argument_count, fiber->stack_top - argument_count);
				fiber->stack_top = slot;

				RETURN_OK(NULL_VALUE)
			}

			case OBJECT_NATIVE_METHOD: {
				LitNativeMethod* method = AS_NATIVE_METHOD(callee);
				LitValue result = method->method(vm, *(fiber->stack_top - argument_count - 1), argument_count, fiber->stack_top - argument_count);
				fiber->stack_top = slot;

				RETURN_OK(result)
			}

			case OBJECT_CLASS: {
				LitClass* klass = AS_CLASS(callee);
				*slot = OBJECT_VALUE(lit_create_instance(vm->state, klass));

				if (klass->init_method != NULL) {
					lit_call(state, OBJECT_VALUE(klass->init_method), arguments, argument_count);
				}

				fiber->stack_top = slot;
				RETURN_OK(*slot);
			}

			case OBJECT_BOUND_METHOD: {
				LitBoundMethod* bound_method = AS_BOUND_METHOD(callee);
				LitValue method = bound_method->method;

				*slot = bound_method->receiver;

				if (IS_NATIVE_METHOD(method)) {
					LitValue result = AS_NATIVE_METHOD(method)->method(vm, bound_method->receiver, argument_count, fiber->stack_top - argument_count);
					fiber->stack_top = slot;

					RETURN_OK(result)
				} else if (IS_PRIMITIVE_METHOD(method)) {
					AS_PRIMITIVE_METHOD(method)->method(vm, bound_method->receiver, argument_count, fiber->stack_top - argument_count);

					fiber->stack_top = slot;
					RETURN_OK(NULL_VALUE)
				} else {
					fiber->stack_top = slot;
					return lit_call_function(state, AS_FUNCTION(method), arguments, argument_count);
				}
			}

			case OBJECT_PRIMITIVE_METHOD: {
				AS_PRIMITIVE_METHOD(callee)->method(vm, *(fiber->stack_top - argument_count - 1), argument_count, fiber->stack_top - argument_count);
				fiber->stack_top = slot;

				RETURN_OK(NULL_VALUE)
			}

			default: {
				break;
			}
		}
	}

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

	if (lit_table_get(&klass->methods, method_name, &method)) {
		return lit_call_method(state, callee, method, arguments, argument_count);
	}

	lit_runtime_error(vm, "Attempt to call method '%s', that is not defined in class %s", method_name->chars, klass->name->chars);
	RETURN_RUNTIME_ERROR()
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
		function->max_slots = 3;

		lit_write_chunk(state, chunk, OP_INVOKE, 1);
		lit_emit_byte(state, chunk, 0);
		lit_emit_short(state, chunk, lit_chunk_add_constant(state, chunk, OBJECT_CONST_STRING(state, "toString")));
		lit_emit_byte(state, chunk, OP_RETURN);
	}

	lit_ensure_fiber_stack(state, fiber, function->max_slots + (int) (fiber->stack_top - fiber->stack));
	LitCallFrame* frame = &fiber->frames[fiber->frame_count++];

	frame->ip = function->chunk.code;
	frame->closure = NULL;
	frame->function = function;
	frame->slots = fiber->stack_top;
	frame->result_ignored = false;
	frame->return_to_c = true;

	PUSH(OBJECT_VALUE(function));
	PUSH(object);

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