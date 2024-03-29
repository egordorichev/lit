#include "lit/api/lit_calls.h"
#include "lit/api/lit_api.h"
#include "lit/debug/lit_debug.h"

#include <math.h>

#define PUSH(value) (*fiber->stack_top++ = value)

static bool ensure_fiber(LitVm* vm, LitFiber* fiber) {
	if (fiber == NULL) {
		lit_runtime_error_exiting(vm, "No fiber to run on");
		return true;
	}

	if (fiber->frame_count == LIT_CALL_FRAMES_MAX) {
		lit_runtime_error_exiting(vm, "Stack overflow");
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

	if (callee == NULL) {
		lit_runtime_error_exiting(vm, "Attempt to call a null value");
		return NULL;
	}

	if (ensure_fiber(vm, fiber)) {
		return NULL;
	}

	LitValue* start = fiber->frame_count > 0 ? fiber->frames[fiber->frame_count - 1].slots + fiber->frames[fiber->frame_count - 1].function->max_registers : fiber->registers;
	lit_ensure_fiber_registers(vm->state, fiber, start - fiber->registers + callee->max_registers);

	LitCallFrame* frame = &fiber->frames[fiber->frame_count++];
	frame->slots = fiber->frame_count > 1 ? fiber->frames[fiber->frame_count - 2].slots + fiber->frames[fiber->frame_count - 2].function->max_registers : fiber->registers;

#ifdef LIT_TRACE_NULL_FILL
	printf("Filling with nulls\n");
#endif

	for (int i = argument_count + 1; i < callee->max_registers; i++) {
		frame->slots[i] = NULL_VALUE;
	}

	frame->slots[0] = OBJECT_VALUE(callee);

	for (uint8_t i = 0; i < argument_count; i++) {
		frame->slots[i + 1] = arguments[i];
	}

	uint target_arg_count = callee->arg_count;
	bool vararg = callee->vararg;

	if (target_arg_count > argument_count) {
#ifdef LIT_TRACE_NULL_FILL
		printf("Filling with nulls\n");
#endif

		for (uint i = argument_count; i < target_arg_count; i++) {
			*(frame->slots + i + 1) = NULL_VALUE;
		}

		if (vararg) {
			*(frame->slots + target_arg_count) = OBJECT_VALUE(lit_create_array(vm->state));
		}
	} else if (vararg) {
		if (target_arg_count == argument_count && IS_VARARG_ARRAY(*(frame->slots + target_arg_count))) {
			// No need to repack the arguments
		} else {
			LitArray *array = &lit_create_vararg_array(vm->state)->array;
			lit_push_root(vm->state, (LitObject*) array);
			lit_values_ensure_size(vm->state, &array->values, argument_count - target_arg_count + 1);

			uint j = 0;

			for (uint i = target_arg_count - 1; i < argument_count; i++) {
				array->values.values[j++] = *(frame->slots + i + 1);
			}

			*(frame->slots + target_arg_count) = OBJECT_VALUE(array);
			lit_pop_root(vm->state);
		}
	}

	frame->ip = callee->chunk.code;
	frame->closure = NULL;
	frame->function = callee;
	frame->result_ignored = false;
	frame->return_to_c = true;
	frame->return_address = NULL;

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

		LitValue* start = fiber->frame_count > 0 ? fiber->frames[fiber->frame_count - 1].slots + fiber->frames[fiber->frame_count - 1].function->max_registers : fiber->registers;
		lit_ensure_fiber_registers(vm->state, fiber, start - fiber->registers + 3 + argument_count);
		LitValue* slot = fiber->frame_count > 0 ? fiber->frames[fiber->frame_count - 1].slots + fiber->frames[fiber->frame_count - 1].function->max_registers : fiber->registers;

#ifdef LIT_TRACE_NULL_FILL
		printf("Filling with nulls\n");
#endif

		for (int i = argument_count; i < argument_count + 3; i++) {
			*(slot + i) = NULL_VALUE;
		}

		*slot = instance;

		if (type != OBJECT_CLASS) {
			for (uint8_t i = 0; i < argument_count; i++) {
				*(slot + i + 1) = arguments[i];
			}
		}

#ifdef LIT_TRACE_EXECUTION
		printf("        |\n        | f%i ", fiber->frame_count);

		for (int i = 0; i <= argument_count; i++) {
			printf("[ ");
			lit_print_value(*(slot + i));
			printf(" ]");
		}

		printf("\n");
#endif

		switch (type) {
			case OBJECT_NATIVE_FUNCTION: {
				// For some reason, single line expression doesn't work
				LitValue value = AS_NATIVE_FUNCTION(callee)->function(vm, argument_count, slot + 1);
				RETURN_OK(value)
			}

			case OBJECT_NATIVE_PRIMITIVE: {
				AS_NATIVE_PRIMITIVE(callee)->function(vm, argument_count, slot + 1);
				RETURN_OK(NULL_VALUE)
			}

			case OBJECT_NATIVE_METHOD: {
				LitNativeMethod* method = AS_NATIVE_METHOD(callee);
				// For some reason, single line expression doesn't work
				LitValue value = method->method(vm, *slot, argument_count, slot + 1);

				RETURN_OK(value)
			}

			case OBJECT_PRIMITIVE_METHOD: {
				AS_PRIMITIVE_METHOD(callee)->method(vm, *slot, argument_count, slot + 1);
				RETURN_OK(NULL_VALUE)
			}

			case OBJECT_CLASS: {
				LitClass* klass = AS_CLASS(callee);
				LitInstance* inst = lit_create_instance(vm->state, klass);

				if (klass->init_method != NULL) {
					lit_call_method(state, *slot, OBJECT_VALUE(klass->init_method), arguments, argument_count);
				}

				RETURN_OK(OBJECT_VALUE(inst))
			}

			case OBJECT_BOUND_METHOD: {
				LitBoundMethod* bound_method = AS_BOUND_METHOD(callee);
				LitValue method = bound_method->method;

				if (IS_NATIVE_METHOD(method)) {
					// For some reason, single line expression doesn't work
					LitValue value = AS_NATIVE_METHOD(method)->method(vm, bound_method->receiver, argument_count, slot + 1);
					RETURN_OK(value)
				} else if (IS_PRIMITIVE_METHOD(method)) {
					AS_PRIMITIVE_METHOD(method)->method(vm, bound_method->receiver, argument_count, slot + 1);
					RETURN_OK(NULL_VALUE)
				} else {
					*slot = bound_method->receiver;
					return lit_call_function(state, AS_FUNCTION(method), arguments, argument_count);
				}
			}

			default: {
				break;
			}
		}
	}

	if (IS_NULL(callee)) {
		lit_runtime_error_exiting(vm, "Attempt to call a null value");
	} else {
		lit_runtime_error_exiting(vm, "Can only call functions and classes");
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
		lit_runtime_error_exiting(vm, "No fiber to run on");
		RETURN_RUNTIME_ERROR()
	}

	LitClass* klass = lit_get_class_for(state, callee);
	LitValue method;

	if ((IS_INSTANCE(callee) && lit_table_get(&AS_INSTANCE(callee)->fields, method_name, &method)) || lit_table_get(&klass->methods, method_name, &method)) {
		return lit_call_method(state, callee, method, arguments, argument_count);
	}

	return (LitInterpretResult) { INTERPRET_INVALID, NULL_VALUE };
}

LitString* lit_to_string(LitState* state, LitValue object, uint indentation) {
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

		return lit_to_string(state, *slot, 0);
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
		function->max_registers = 3;

		int constant = lit_chunk_add_constant(state, chunk, OBJECT_CONST_STRING(state, "toString"));

		lit_write_chunk(state, chunk, LIT_FORM_ABC_INSTRUCTION(OP_INVOKE, 1, 2, constant), 1);
		lit_write_chunk(state, chunk, LIT_FORM_ABC_INSTRUCTION(OP_RETURN, 1, 0, 0), 1);
	}

	lit_ensure_fiber_registers(vm->state, fiber, (fiber->frame_count > 0 ? (fiber->frames[fiber->frame_count - 1].slots + (int) fiber->frames[fiber->frame_count - 1].function->max_registers) : fiber->registers) - fiber->registers + function->max_registers);
	LitCallFrame* frame = &fiber->frames[fiber->frame_count++];

	frame->ip = function->chunk.code;
	frame->closure = NULL;
	frame->function = function;

	// "Duplicated" code due to lit_ensure_fiber_registers messing with register pointers
	frame->slots = (fiber->frame_count > 1 ? (fiber->frames[fiber->frame_count - 2].slots + (int) fiber->frames[fiber->frame_count - 2].function->max_registers) : fiber->registers);
	frame->result_ignored = false;
	frame->return_to_c = true;
	frame->return_address = NULL;

	frame->slots[0] = OBJECT_VALUE(function);
	frame->slots[1] = object;
	frame->slots[2] = NUMBER_VALUE(indentation);

	LitInterpretResult result = lit_interpret_fiber(state, fiber);

	if (result.type != INTERPRET_OK) {
		return CONST_STRING(state, "null");
	}

	if (!IS_STRING(result.result)) {
		return CONST_STRING(state, "invalid toString()");
	}

	return AS_STRING(result.result);
}

LitValue lit_call_new(LitVm* vm, const char* name, LitValue* args, uint argument_count) {
	LitValue value;

	if (!lit_table_get(&vm->globals->values, CONST_STRING(vm->state, name), &value)) {
		lit_runtime_error_exiting(vm, "Failed to create instance of class %s: class not found", name);
		return NULL_VALUE;
	}

	LitClass* klass = AS_CLASS(value);

	if (klass->init_method == NULL) {
		return OBJECT_VALUE(lit_create_instance(vm->state, klass));
	}

	return lit_call_method(vm->state, value, value, args, argument_count).result;
}

#undef PUSH