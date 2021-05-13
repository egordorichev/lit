#include "vm/lit_vm.h"
#include "vm/lit_object.h"
#include"debug/lit_debug.h"
#include "mem/lit_mem.h"

#include <stdio.h>
#include <math.h>
#include <string.h>
#include <setjmp.h>

#ifdef LIT_TRACE_EXECUTION
	#define TRACE_FRAME() lit_trace_frame(fiber);
#else
	#define TRACE_FRAME() do {} while (0);
#endif

#define PUSH_GC(state, allow) bool was_allowed = state->allow_gc; state->allow_gc = allow;
#define POP_GC(state) state->allow_gc = was_allowed;

static jmp_buf jump_buffer;

static void reset_stack(LitVm* vm) {
	if (vm->fiber != NULL) {
		vm->fiber->stack_top = vm->fiber->stack;
	}
}

static void reset_vm(LitState* state, LitVm* vm) {
	vm->state = state;
	vm->objects = NULL;
	vm->fiber = NULL;

	vm->gray_stack = NULL;
	vm->gray_count = 0;
	vm->gray_capacity = 0;

	lit_init_table(&vm->strings);

	vm->globals = NULL;
	vm->modules = NULL;
}

void lit_init_vm(LitState* state, LitVm* vm) {
	reset_vm(state, vm);

	vm->globals = lit_create_map(state);
	vm->modules = lit_create_map(state);
}

void lit_free_vm(LitVm* vm) {
	lit_free_table(vm->state, &vm->strings);
	lit_free_objects(vm->state, vm->objects);

	reset_vm(vm->state, vm);
}

void lit_trace_vm_stack(LitVm* vm) {
	LitFiber* fiber = vm->fiber;

	if (fiber->stack_top == fiber->stack || fiber->frame_count == 0) {
		return;
	}

	LitValue* top = fiber->frames[fiber->frame_count - 1].slots;
	printf("        | %s", COLOR_GREEN);

	for (LitValue* slot = fiber->stack; slot < top; slot++) {
		printf("[ ");
		lit_print_value(*slot);
		printf(" ]");
	}

	printf("%s", COLOR_RESET);

	for (LitValue* slot = top; slot < fiber->stack_top; slot++) {
		printf("[ ");
		lit_print_value(*slot);
		printf(" ]");
	}

	printf("\n");
}

bool lit_handle_runtime_error(LitVm* vm, LitString* error_string) {
	LitValue error = OBJECT_VALUE(error_string);
	LitFiber* fiber = vm->fiber;

	while (fiber != NULL) {
		fiber->error = error;

		if (fiber->catcher) {
			vm->fiber = fiber->parent;
			vm->fiber->stack_top -= fiber->arg_count;
			vm->fiber->stack_top[-1] = error;

			return true;
		}

		LitFiber* caller = fiber->parent;
		fiber->parent = NULL;
		fiber = caller;
	}

	fiber = vm->fiber;
	fiber->abort = true;
	fiber->error = error;

	if (fiber->parent != NULL) {
		fiber->parent->abort = true;
	}

	// Maan, formatting c strings is hard...
	int count = (int) fiber->frame_count - 1;
	size_t length = snprintf(NULL, 0, "%s%s\n", COLOR_RED, error_string->chars);

	for (int i = count; i >= 0; i--) {
		LitCallFrame* frame = &fiber->frames[i];
		LitFunction* function = frame->function;
		LitChunk* chunk = &function->chunk;
		const char* name = function->name == NULL ? "unknown" : function->name->chars;

		if (chunk->has_line_info) {
			length += snprintf(NULL, 0, "[line %d] in %s()\n", lit_chunk_get_line(chunk, frame->ip - chunk->code - 1), name);
		} else {
			length += snprintf(NULL, 0, "\tin %s()\n", name);
		}
	}

	length += snprintf(NULL, 0, "%s", COLOR_RESET);
	char buffer[length + 1];
	buffer[length] = '\0';

	char* start = buffer + sprintf(buffer, "%s%s\n", COLOR_RED, error_string->chars);

	for (int i = count; i >= 0; i--) {
		LitCallFrame* frame = &fiber->frames[i];
		LitFunction* function = frame->function;
		LitChunk* chunk = &function->chunk;
		const char* name = function->name == NULL ? "unknown" : function->name->chars;

		if (chunk->has_line_info) {
			start += sprintf(start, "[line %d] in %s()\n", lit_chunk_get_line(chunk, frame->ip - chunk->code - 1), name);
		} else {
			start += sprintf(start, "\tin %s()\n", name);
		}
	}

	start += sprintf(start, "%s", COLOR_RESET);
	lit_error(vm->state, RUNTIME_ERROR, buffer);
	reset_stack(vm);

	return false;
}

bool lit_vruntime_error(LitVm* vm, const char* format, va_list args) {
	va_list args_copy;
	va_copy(args_copy, args);
	size_t buffer_size = vsnprintf(NULL, 0, format, args_copy) + 1;
	va_end(args_copy);

	char buffer[buffer_size];
	vsnprintf(buffer, buffer_size, format, args);

	return lit_handle_runtime_error(vm, lit_copy_string(vm->state, buffer, buffer_size));
}

bool lit_runtime_error(LitVm* vm, const char* format, ...) {
	va_list args;
	va_start(args, format);
	bool result = lit_vruntime_error(vm, format, args);
	va_end(args);

	return result;
}

bool lit_runtime_error_exiting(LitVm* vm, const char* format, ...) {
	va_list args;
	va_start(args, format);
	bool result = lit_vruntime_error(vm, format, args);
	va_end(args);

	lit_native_exit_jump();
	return result;
}

static bool call(LitVm* vm, register LitFunction* function, LitClosure* closure, uint8_t arg_count) {
	register LitFiber* fiber = vm->fiber;

	if (fiber->frame_count == LIT_CALL_FRAMES_MAX) {
		lit_runtime_error(vm, "Stack overflow");
		return true;
	}

	if (fiber->frame_count + 1 > fiber->frame_capacity) {
		uint new_capacity = fmin(LIT_CALL_FRAMES_MAX, fiber->frame_capacity * 2);
		fiber->frames = (LitCallFrame*) lit_reallocate(vm->state, fiber->frames, sizeof(LitCallFrame) * fiber->frame_capacity, sizeof(LitCallFrame) * new_capacity);
		fiber->frame_capacity = new_capacity;
	}

	uint function_arg_count = function->arg_count;
	lit_ensure_fiber_stack(vm->state, fiber, function->max_slots + (int) (fiber->stack_top - fiber->stack));

	register LitCallFrame* frame = &fiber->frames[fiber->frame_count++];

	frame->function = function;
	frame->closure = closure;
	frame->ip = function->chunk.code;
	frame->slots = fiber->stack_top - arg_count - 1;
	frame->result_ignored = false;
	frame->return_to_c = false;

	if (arg_count != function_arg_count) {
		bool vararg = function->vararg;

		if (arg_count < function_arg_count) {
			int amount = (int) function_arg_count - arg_count - (vararg ? 1 : 0);

			for (int i = 0; i < amount; i++) {
				lit_push(vm, NULL_VALUE);
			}

			if (vararg) {
				lit_push(vm, OBJECT_VALUE(lit_create_array(vm->state)));
			}
		} else if (function->vararg) {
			LitArray* array = lit_create_array(vm->state);
			uint vararg_count = arg_count - function_arg_count + 1;

			lit_push_root(vm->state, (LitObject*) array);
			lit_values_ensure_size(vm->state, &array->values, vararg_count);
			lit_pop_root(vm->state);

			for (uint i = 0; i < vararg_count; i++) {
				array->values.values[i] = vm->fiber->stack_top[(int) i - (int) vararg_count];
			}

			vm->fiber->stack_top -= vararg_count;
			lit_push(vm, OBJECT_VALUE(array));
		} else {
			vm->fiber->stack_top -= (arg_count - function_arg_count);
		}
	} else if (function->vararg) {
		LitArray* array = lit_create_array(vm->state);
		uint vararg_count = arg_count - function_arg_count + 1;

		lit_push_root(vm->state, (LitObject*) array);
		lit_values_write(vm->state, &array->values, *(fiber->stack_top - 1));
		*(fiber->stack_top - 1) = OBJECT_VALUE(array);
		lit_pop_root(vm->state);
	}

	return true;
}

static bool call_value(LitVm* vm, LitValue callee, uint8_t arg_count) {
	if (IS_OBJECT(callee)) {
		if (lit_set_native_exit_jump()) {
			return true;
		}

		switch (OBJECT_TYPE(callee)) {
			case OBJECT_FUNCTION: {
				return call(vm, AS_FUNCTION(callee), NULL, arg_count);
			}

			case OBJECT_CLOSURE: {
				LitClosure* closure = AS_CLOSURE(callee);
				return call(vm, closure->function, closure, arg_count);
			}

			case OBJECT_NATIVE_FUNCTION: {
				PUSH_GC(vm->state, false)

				LitValue result = AS_NATIVE_FUNCTION(callee)->function(vm, arg_count, vm->fiber->stack_top - arg_count);
				vm->fiber->stack_top -= arg_count + 1;
				lit_push(vm, result);

				POP_GC(vm->state)
				return false;
			}

			case OBJECT_NATIVE_PRIMITIVE: {
				PUSH_GC(vm->state, false)

				LitFiber* fiber = vm->fiber;
				bool result = AS_NATIVE_PRIMITIVE(callee)->function(vm, arg_count, fiber->stack_top - arg_count);

				if (result) {
					fiber->stack_top -= arg_count;
				}

				POP_GC(vm->state)
				return result;
			}

			case OBJECT_NATIVE_METHOD: {
				PUSH_GC(vm->state, false)

				LitNativeMethod* method = AS_NATIVE_METHOD(callee);
				LitFiber* fiber = vm->fiber;
				LitValue result = method->method(vm, *(vm->fiber->stack_top - arg_count - 1), arg_count, vm->fiber->stack_top - arg_count);

				vm->fiber->stack_top -= arg_count + 1;
				lit_push(vm, result);

				POP_GC(vm->state)
				return false;
			}

			case OBJECT_PRIMITIVE_METHOD: {
				PUSH_GC(vm->state, false)

				LitFiber* fiber = vm->fiber;
				bool result = AS_PRIMITIVE_METHOD(callee)->method(vm, *(fiber->stack_top - arg_count - 1), arg_count, fiber->stack_top - arg_count);

				if (result) {
					fiber->stack_top -= arg_count;
				}

				POP_GC(vm->state)
				return result;
			}

			case OBJECT_CLASS: {
				LitClass* klass = AS_CLASS(callee);
				LitInstance* instance = lit_create_instance(vm->state, klass);

				vm->fiber->stack_top[-arg_count - 1] = OBJECT_VALUE(instance);

				if (klass->init_method != NULL) {
					return call_value(vm, OBJECT_VALUE(klass->init_method), arg_count);
				}

				// Remove the arguments, so that they don't mess up the stack
				// (default constructor has no arguments)
				for (uint i = 0; i < arg_count; i++) {
					lit_pop(vm);
				}

				return false;
			}

			case OBJECT_BOUND_METHOD: {
				LitBoundMethod* bound_method = AS_BOUND_METHOD(callee);
				LitValue method = bound_method->method;

				if (IS_NATIVE_METHOD(method)) {
					PUSH_GC(vm->state, false)

					LitValue result = AS_NATIVE_METHOD(method)->method(vm, bound_method->receiver, arg_count, vm->fiber->stack_top - arg_count);
					vm->fiber->stack_top -= arg_count + 1;
					lit_push(vm, result);

					POP_GC(vm->state)
					return false;
				} else if (IS_PRIMITIVE_METHOD(method)) {
					LitFiber* fiber = vm->fiber;
					PUSH_GC(vm->state, false)

					if (AS_PRIMITIVE_METHOD(method)->method(vm, bound_method->receiver, arg_count, fiber->stack_top - arg_count)) {
						fiber->stack_top -= arg_count;
						return true;
					}

					POP_GC(vm->state)
					return false;
				} else {
					vm->fiber->stack_top[-arg_count - 1] = bound_method->receiver;
					return call(vm, AS_FUNCTION(method), NULL, arg_count);
				}
			}

			default: {
				break;
			}
		}
	}

	if (IS_NULL(callee)) {
		lit_runtime_error(vm, "Attempt to call a null value");
	} else {
		lit_runtime_error(vm, "Can only call functions and classes, got %s", lit_get_value_type(callee));
	}

	return true;
}

static LitUpvalue* capture_upvalue(LitState* state, LitValue* local) {
	LitUpvalue* previous_upvalue = NULL;
	LitUpvalue* upvalue = state->vm->fiber->open_upvalues;

	while (upvalue != NULL && upvalue->location > local) {
		previous_upvalue = upvalue;
		upvalue = upvalue->next;
	}

	if (upvalue != NULL && upvalue->location == local) {
		return upvalue;
	}

	LitUpvalue* created_upvalue = lit_create_upvalue(state, local);
	created_upvalue->next = upvalue;

	if (previous_upvalue == NULL) {
		state->vm->fiber->open_upvalues = created_upvalue;
	} else {
		previous_upvalue->next = created_upvalue;
	}

	return created_upvalue;
}

static void close_upvalues(register LitVm* vm, const LitValue* last) {
	LitFiber* fiber = vm->fiber;

	while (fiber->open_upvalues != NULL && fiber->open_upvalues->location >= last) {
		LitUpvalue* upvalue = fiber->open_upvalues;

		upvalue->closed = *upvalue->location;
		upvalue->location = &upvalue->closed;

		fiber->open_upvalues = upvalue->next;
	}
}

LitInterpretResult lit_interpret_module(LitState* state, LitModule* module) {
	register LitVm *vm = state->vm;

	LitFiber* fiber = lit_create_fiber(state, module, module->main_function);
	vm->fiber = fiber;

	lit_push(vm, OBJECT_VALUE(module->main_function));
	LitInterpretResult result = lit_interpret_fiber(state, fiber);

	return result;
}

LitInterpretResult lit_interpret_fiber(LitState* state, register LitFiber* fiber) {
	register LitVm *vm = state->vm;
	PUSH_GC(state, true);

	vm->fiber = fiber;
	fiber->abort = false;

	register LitCallFrame* frame = &fiber->frames[fiber->frame_count - 1];
	register LitChunk* current_chunk = &frame->function->chunk;

	fiber->module = frame->function->module;

	register uint8_t* ip = frame->ip;
	register LitValue* slots = frame->slots;
	register LitValue* privates = fiber->module->privates;
	register LitUpvalue** upvalues = frame->closure == NULL ? NULL : frame->closure->upvalues;

	// Has to be inside of the function in order for goto to work
	static void* dispatch_table[] = {
/*#define OPCODE(name, effect) &&OP_##name,
#include "vm/lit_opcodes.h"
#undef OPCODE*/
	};

#define PUSH(value) (*fiber->stack_top++ = value)
#define POP() (*(--fiber->stack_top))
#define DROP() (fiber->stack_top--)
#define DROP_MULTIPLE(amount) (fiber->stack_top -= amount)
#define READ_BYTE() (*ip++)
#define READ_SHORT() (ip += 2u, (uint16_t) ((ip[-2] << 8u) | ip[-1]))

#define CASE_CODE(name) OP_##name:
#define READ_CONSTANT() (current_chunk->constants.values[READ_BYTE()])
#define READ_CONSTANT_LONG() (current_chunk->constants.values[READ_SHORT()])
#define READ_STRING() AS_STRING(READ_CONSTANT())
#define READ_STRING_LONG() AS_STRING(READ_CONSTANT_LONG())
#define PEEK(distance) fiber->stack_top[-1 - distance]
#define READ_FRAME() frame = &fiber->frames[fiber->frame_count - 1]; \
	current_chunk = &frame->function->chunk; \
	ip = frame->ip; \
	slots = frame->slots; \
	fiber->module = frame->function->module; \
	privates = fiber->module->privates; \
	upvalues = frame->closure == NULL ? NULL : frame->closure->upvalues;

#define WRITE_FRAME() frame->ip = ip;
#define RETURN_ERROR() POP_GC(state) return (LitInterpretResult) { INTERPRET_RUNTIME_ERROR, NULL_VALUE };

#define RECOVER_STATE() \
	WRITE_FRAME() \
	fiber = vm->fiber; \
	if (fiber == NULL) { \
		return (LitInterpretResult) { INTERPRET_OK, POP() }; \
	} \
	if (fiber->abort) { \
		RETURN_ERROR() \
	} \
	READ_FRAME() \
	TRACE_FRAME()

#define RUNTIME_ERROR(format) \
	if (lit_runtime_error(vm, format)) { \
		RECOVER_STATE() \
		continue; \
	} else { \
		RETURN_ERROR() \
	}

#define RUNTIME_ERROR_VARG(format, ...) \
	if (lit_runtime_error(vm, format, __VA_ARGS__)) { \
		RECOVER_STATE() \
		continue; \
	} else { \
		RETURN_ERROR() \
	}

#ifdef LIT_TRACE_EXECUTION
	TRACE_FRAME()
	uint8_t instruction;
#endif

	while (true) {
#ifdef LIT_TRACE_STACK
		lit_trace_vm_stack(vm);
#endif

#ifdef LIT_CHECK_STACK_SIZE
		if ((fiber->stack_top - frame->slots) > fiber->stack_capacity) {
			RUNTIME_ERROR_VARG("Fiber stack is not large enough (%i > %i)", (int) (fiber->stack_top - frame->slots), fiber->stack_capacity)
		}
#endif

#ifdef LIT_TRACE_EXECUTION
		instruction = *ip++;

		lit_disassemble_instruction(current_chunk, (uint) (ip - current_chunk->code - 1), NULL);
		// goto *dispatch_table[instruction];
#else
		goto *dispatch_table[*ip++];
#endif


		RUNTIME_ERROR_VARG("Unknown op code '%d'", *ip)
		break;
	}

#undef RUNTIME_ERROR_VARG
#undef RUNTIME_ERROR
#undef INVOKE_METHOD
#undef INVOKE_FROM_CLASS
#undef INVOKE_FROM_CLASS_ADVANCED
#undef DROP_MULTIPLE
#undef PUSH
#undef DROP
#undef POP
#undef CALL_VALUE
#undef RECOVER_STATE
#undef WRITE_FRAME
#undef READ_FRAME
#undef PEEK
#undef BITWISE_OP
#undef BINARY_OP
#undef READ_CONSTANT_LONG
#undef READ_CONSTANT
#undef CASE_CODE
#undef READ_STRING
#undef READ_SHORT
#undef READ_BYTE

	RETURN_ERROR()

#undef RETURN_ERROR
}

void lit_native_exit_jump() {
	longjmp(jump_buffer, 1);
}

bool lit_set_native_exit_jump() {
	return setjmp(jump_buffer);
}

#undef PUSH_GC
#undef POP_GC
#undef LIT_TRACE_FRAME