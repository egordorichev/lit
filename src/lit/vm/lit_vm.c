#include <lit/vm/lit_vm.h>
#include <lit/vm/lit_object.h>
#include <lit/debug/lit_debug.h>
#include <lit/mem/lit_mem.h>

#include <stdio.h>
#include <math.h>
#include <string.h>
#include <setjmp.h>

#ifdef LIT_TRACE_EXECUTION
	#define TRACE_FRAME() printf("== f%i %s (expects %i, max %i, added %i, current %i) ==\n", fiber->frame_count - 1, frame->function->name->chars, frame->function->arg_count, frame->function->max_slots, frame->function->max_slots + (int) (fiber->stack_top - fiber->stack), fiber->stack_capacity);
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

static void trace_stack(LitVm* vm) {
	LitFiber* fiber = vm->fiber;

	if (fiber->stack_top == fiber->stack) {
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

	if (fiber->parent != NULL) {
		fiber->parent->abort = true;
	}

	fprintf(stderr, "%s%s\n", COLOR_RED, error_string->chars);

	for (int i = (int) fiber->frame_count - 1; i >= 0; i--) {
		LitCallFrame* frame = &fiber->frames[i];
		LitFunction* function = frame->function;
		LitChunk* chunk = &function->chunk;
		const char* name = function->name == NULL ? "unknown" : function->name->chars;

		if (chunk->has_line_info) {
			fprintf(stderr, "[line %d] in %s()\n", lit_chunk_get_line(chunk, frame->ip - chunk->code - 1), name);
		} else {
			fprintf(stderr, "\tin %s()\n", name);
		}
	}

	fprintf(stderr, "%s", COLOR_RESET);
	reset_stack(vm);

	return false;
}

bool lit_runtime_error(LitVm* vm, const char* format, ...) {
	LitFiber* fiber = vm->fiber;

	va_list args;
	va_start(args, format);
	size_t buffer_size = vsnprintf(NULL, 0, format, args) + 1;
	va_end(args);

	char buffer[buffer_size];

	va_start(args, format);
	vsnprintf(buffer, buffer_size, format, args);
	va_end(args);

	return lit_handle_runtime_error(vm, lit_copy_string(vm->state, buffer, buffer_size));
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
	}

	return true;
}

static bool call_value(LitVm* vm, LitValue callee, uint8_t arg_count) {
	if (IS_OBJECT(callee)) {
		if (setjmp(jump_buffer)) {
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
				LitFiber* fiber = vm->fiber;
				PUSH_GC(vm->state, false)

				if (AS_NATIVE_PRIMITIVE(callee)->function(vm, arg_count, fiber->stack_top - arg_count)) {
					fiber->stack_top -= arg_count;
					POP_GC(vm->state)
					return true;
				}

				POP_GC(vm->state)
				return false;
			}

			case OBJECT_NATIVE_METHOD: {
				LitNativeMethod* method = AS_NATIVE_METHOD(callee);

				LitFiber* fiber = vm->fiber;
				PUSH_GC(vm->state, false)
				LitValue result = method->method(vm, *(vm->fiber->stack_top - arg_count - 1), arg_count, vm->fiber->stack_top - arg_count);

				vm->fiber->stack_top -= arg_count + 1;
				lit_push(vm, result);
				POP_GC(vm->state)

				return false;
			}

			case OBJECT_PRIMITIVE_METHOD: {
				LitFiber* fiber = vm->fiber;
				PUSH_GC(vm->state, false)

				if (AS_PRIMITIVE_METHOD(callee)->method(vm, *(fiber->stack_top - arg_count - 1), arg_count, fiber->stack_top - arg_count)) {
					fiber->stack_top -= arg_count;
					POP_GC(vm->state)
					return true;
				}

				POP_GC(vm->state)
				return false;
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
		lit_runtime_error(vm, "Can only call functions and classes");
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
	fiber->parent = vm->fiber;
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
#define OPCODE(name, effect) &&OP_##name,
#include <lit/vm/lit_opcodes.h>
#undef OPCODE
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

#define CALL_VALUE(callee, arg_count) \
	if (call_value(vm, callee, arg_count)) { \
		RECOVER_STATE() \
	}

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

#define INVOKE_FROM_CLASS_ADVANCED(zklass, method_name, arg_count, error, stat, ignoring, callee) \
	LitValue method; \
	if ((IS_INSTANCE(callee) && (lit_table_get(&AS_INSTANCE(callee)->fields, method_name, &method))) || lit_table_get(&zklass->stat, method_name, &method)) { \
		if (ignoring) { \
			if (call_value(vm, method, arg_count)) { \
				RECOVER_STATE() \
				frame->result_ignored = true; \
			} else { \
				fiber->stack_top[-1] = callee; \
			} \
		} else { \
			CALL_VALUE(method, arg_count) \
		} \
	} else { \
		if (error) { \
			RUNTIME_ERROR_VARG("Attempt to call method '%s', that is not defined in class %s", method_name->chars, zklass->name->chars) \
		} \
	} \
	if (error) { \
		continue; \
	} \

#define INVOKE_FROM_CLASS(klass, method_name, arg_count, error, stat, ignoring) INVOKE_FROM_CLASS_ADVANCED(klass, method_name, arg_count, error, stat, ignoring, PEEK(arg_count))

#define INVOKE_METHOD(instance, method_name, arg_count) \
	LitClass* klass = lit_get_class_for(state, instance); \
	if (klass == NULL) { \
		RUNTIME_ERROR("Only instances and classes have methods") \
	} \
	WRITE_FRAME() \
	INVOKE_FROM_CLASS_ADVANCED(klass, CONST_STRING(state, method_name), arg_count, true, methods, false, instance) \
	READ_FRAME()

#define BINARY_OP(type, op, op_string) \
	LitValue a = PEEK(1); \
	LitValue b = PEEK(0); \
	if (IS_NUMBER(a)) { \
		if (!IS_NUMBER(b)) { \
			RUNTIME_ERROR_VARG("Attempt to use operator %s with a number and not a number", op_string) \
		} \
		DROP(); \
		*(fiber->stack_top - 1) = (type(AS_NUMBER(a) op AS_NUMBER(b))); \
		continue; \
	} \
	if (IS_NULL(a)) { \
		RUNTIME_ERROR_VARG("Attempt to use operator %s on a null value", op_string) \
	} \
	INVOKE_METHOD(a, op_string, 1)


#define BITWISE_OP(op, op_string) \
	LitValue a = PEEK(1); \
	LitValue b = PEEK(0); \
	if (!IS_NUMBER(a) || !IS_NUMBER(b)) { \
		RUNTIME_ERROR_VARG("Operands of bitwise operator %s must be two numbers", op_string) \
	} \
	DROP(); \
	*(fiber->stack_top - 1) = (NUMBER_VALUE((int) AS_NUMBER(a) op (int) AS_NUMBER(b)));

#define INVOKE_OPERATION(ignoring) \
	uint8_t arg_count = READ_BYTE(); \
	LitString* method_name = READ_STRING_LONG(); \
	LitValue receiver = PEEK(arg_count); \
	if (IS_NULL(receiver)) { \
		RUNTIME_ERROR("Attempt to index a null value") \
	} \
	WRITE_FRAME() \
	if (IS_CLASS(receiver)) { \
		INVOKE_FROM_CLASS_ADVANCED(AS_CLASS(receiver), method_name, arg_count, true, static_fields, ignoring, receiver) \
		continue; \
	} else if (IS_INSTANCE(receiver)) { \
		LitInstance* instance = AS_INSTANCE(receiver); \
		LitValue value; \
		if (lit_table_get(&instance->fields, method_name, &value)) { \
			fiber->stack_top[-arg_count - 1] = value; \
			CALL_VALUE(value, arg_count) \
			READ_FRAME() \
			continue; \
		} \
		INVOKE_FROM_CLASS_ADVANCED(instance->klass, method_name, arg_count, true, methods, ignoring, receiver) \
	} else { \
		LitClass* type = lit_get_class_for(state, receiver); \
		if (type == NULL) { \
			RUNTIME_ERROR("Only instances and classes have methods") \
		} \
		INVOKE_FROM_CLASS_ADVANCED(type, method_name, arg_count, true, methods, ignoring, receiver) \
	}

#ifdef LIT_TRACE_EXECUTION
	TRACE_FRAME()
	uint8_t instruction;
#endif

	while (true) {
#ifdef LIT_TRACE_STACK
		trace_stack(vm);
#endif

#ifdef LIT_CHECK_STACK_SIZE
		if ((fiber->stack_top - frame->slots) > fiber->stack_capacity) {
			RUNTIME_ERROR_VARG("Fiber stack is not large enough (%i > %i)", (int) (fiber->stack_top - frame->slots), fiber->stack_capacity)
		}
#endif

#ifdef LIT_TRACE_EXECUTION
		instruction = *ip++;

		lit_disassemble_instruction(current_chunk, (uint) (ip - current_chunk->code - 1), NULL);
		goto *dispatch_table[instruction];
#else
		goto *dispatch_table[*ip++];
#endif

		CASE_CODE(POP) {
			DROP();
			continue;
		}

		CASE_CODE(RETURN) {
			LitValue result = POP();
			close_upvalues(vm, slots);

			WRITE_FRAME()
			fiber->frame_count--;

			if (fiber->frame_count == 0) {
				fiber->module->return_value = result;

				if (fiber->parent == NULL) {
					DROP();

					#ifdef LIT_TRACE_EXECUTION
					printf("== end ==\n");
					#endif

					state->allow_gc = was_allowed;
					vm->fiber = NULL;

					return (LitInterpretResult) { INTERPRET_OK, result };
				}

				uint arg_count = fiber->arg_count;
				LitFiber *parent = fiber->parent;
				fiber->parent = NULL;

				vm->fiber = fiber = parent;

				READ_FRAME()
				TRACE_FRAME()

				fiber->stack_top -= arg_count;
				fiber->stack_top[-1] = result;

				continue;
			}

			fiber->stack_top = frame->slots;

			if (frame->result_ignored) {
				fiber->stack_top++;
				frame->result_ignored = false;
			} else {
				PUSH(result);
			}
		frame = &fiber->frames[fiber->frame_count - 1]; \
	current_chunk = &frame->function->chunk; \
	ip = frame->ip; \
	slots = frame->slots; \
	fiber->module = frame->function->module; \
	privates = fiber->module->privates; \
	upvalues = frame->closure == NULL ? NULL : frame->closure->upvalues;
			TRACE_FRAME()

			continue;
		}

		CASE_CODE(CONSTANT) {
			PUSH(READ_CONSTANT());
			continue;
		}

		CASE_CODE(CONSTANT_LONG) {
			PUSH(READ_CONSTANT_LONG());
			continue;
		}

		CASE_CODE(TRUE) {
			PUSH(TRUE_VALUE);
			continue;
		}

		CASE_CODE(FALSE) {
			PUSH(FALSE_VALUE);
			continue;
		}

		CASE_CODE(NULL) {
			PUSH(NULL_VALUE);
			continue;
		}

		CASE_CODE(ARRAY) {
			PUSH(OBJECT_VALUE(lit_create_array(state)));
			continue;
		}

		CASE_CODE(MAP) {
			PUSH(OBJECT_VALUE(lit_create_map(state)));
			continue;
		}

		CASE_CODE(RANGE) {
			LitValue a = POP();
			LitValue b = POP();

			if (!IS_NUMBER(a) || !IS_NUMBER(b)) {
				RUNTIME_ERROR("Range operands must be number")
			}

			PUSH(OBJECT_VALUE(lit_create_range(state, AS_NUMBER(a), AS_NUMBER(b))));
			continue;
		}

		CASE_CODE(NEGATE) {
			if (!IS_NUMBER(PEEK(0))) {
				LitValue arg = PEEK(0);

				// Don't even ask me why
				// This doesn't kill our performance, since it's a error anyway
				if (IS_STRING(arg) && strcmp(AS_CSTRING(arg), "muffin") == 0) {
					RUNTIME_ERROR("Idk, can you negate a muffin?")
				} else {
					RUNTIME_ERROR("Operand must be a number")
				}
			}

			PUSH(NUMBER_VALUE(-AS_NUMBER(POP())));
			continue;
		}

		CASE_CODE(NOT) {
			if (IS_INSTANCE(PEEK(0))) {
				WRITE_FRAME()
				INVOKE_FROM_CLASS(AS_INSTANCE(PEEK(0))->klass, CONST_STRING(state, "!"), 0, false, methods, false)
				continue;
			}

			PUSH(BOOL_VALUE(lit_is_falsey(POP())));
			continue;
		}

		CASE_CODE(BNOT) {
			if (!IS_NUMBER(PEEK(0))) {
				RUNTIME_ERROR("Operand must be a number")
			}

			PUSH(NUMBER_VALUE(~((int) AS_NUMBER(POP()))));
			continue;
		}

		CASE_CODE(ADD) {
			BINARY_OP(NUMBER_VALUE, +, "+")
			continue;
		}

		CASE_CODE(SUBTRACT) {
			BINARY_OP(NUMBER_VALUE, -, "-")
			continue;
		}

		CASE_CODE(MULTIPLY) {
			BINARY_OP(NUMBER_VALUE, *, "*")
			continue;
		}

		CASE_CODE(POWER) {
			LitValue a = PEEK(1);
			LitValue b = PEEK(0);

			if (IS_NUMBER(a) && IS_NUMBER(b)) {
				DROP();
				*(fiber->stack_top - 1) = (NUMBER_VALUE(pow(AS_NUMBER(a), AS_NUMBER(b))));

				continue;
			}

			INVOKE_METHOD(a, "**", 1)
			continue;
		}

		CASE_CODE(DIVIDE) {
			BINARY_OP(NUMBER_VALUE, /, "/")
			continue;
		}

		CASE_CODE(FLOOR_DIVIDE) {
			LitValue a = PEEK(1);
			LitValue b = PEEK(0);

			if (IS_NUMBER(a) && IS_NUMBER(b)) {
				DROP();
				*(fiber->stack_top - 1) = (NUMBER_VALUE(floor(AS_NUMBER(a) / AS_NUMBER(b))));

				continue;
			}

			INVOKE_METHOD(a, "#", 1)
			continue;
		}

		CASE_CODE(MOD) {
			LitValue a = PEEK(1);
			LitValue b = PEEK(0);

			if (IS_NUMBER(a) && IS_NUMBER(b)) {
				DROP();
				*(fiber->stack_top - 1) = NUMBER_VALUE(fmod(AS_NUMBER(a), AS_NUMBER(b)));
				continue;
			}

			INVOKE_METHOD(a, "%", 1)
			continue;
		}

		CASE_CODE(BAND) {
			BITWISE_OP(&, "&")
			continue;
		}

		CASE_CODE(BOR) {
			BITWISE_OP(|, "|")
			continue;
		}

		CASE_CODE(BXOR) {
			BITWISE_OP(^, "^")
			continue;
		}

		CASE_CODE(LSHIFT) {
			BITWISE_OP(<<, "<<")
			continue;
		}

		CASE_CODE(RSHIFT) {
			BITWISE_OP(>>, ">>")
			continue;
		}

		CASE_CODE(EQUAL) {
			if (IS_INSTANCE(PEEK(1))) {
				WRITE_FRAME()
				INVOKE_FROM_CLASS(AS_INSTANCE(PEEK(1))->klass, CONST_STRING(state, "=="), 1, false, methods, false)
				continue;
			}

			LitValue a = POP();
			LitValue b = POP();

			PUSH(BOOL_VALUE(a == b));
			continue;
		}

		CASE_CODE(GREATER) {
			BINARY_OP(BOOL_VALUE, >, ">")
			continue;
		}

		CASE_CODE(GREATER_EQUAL) {
			BINARY_OP(BOOL_VALUE, >=, ">=")
			continue;
		}

		CASE_CODE(LESS) {
			BINARY_OP(BOOL_VALUE, <, "<")
			continue;
		}

		CASE_CODE(LESS_EQUAL) {
			BINARY_OP(BOOL_VALUE, <=, "<=")
			continue;
		}

		CASE_CODE(SET_GLOBAL) {
			LitString* name = READ_STRING_LONG();
			lit_table_set(state, &vm->globals->values, name, PEEK(0));

			continue;
		}

		CASE_CODE(GET_GLOBAL) {
			LitString* name = READ_STRING_LONG();
			LitValue value;

			if (!lit_table_get(&vm->globals->values, name, &value)) {
				PUSH(NULL_VALUE);
			} else {
				PUSH(value);
			}

			continue;
		}

		CASE_CODE(SET_LOCAL) {
			uint8_t index = READ_BYTE();
			slots[index] = PEEK(0);

			continue;
		}

		CASE_CODE(GET_LOCAL) {
			PUSH(slots[READ_BYTE()]);
			continue;
		}

		CASE_CODE(SET_LOCAL_LONG) {
			uint8_t index = READ_SHORT();
			slots[index] = PEEK(0);

			continue;
		}

		CASE_CODE(GET_LOCAL_LONG) {
			PUSH(slots[READ_SHORT()]);
			continue;
		}

		CASE_CODE(SET_PRIVATE) {
			uint8_t index = READ_BYTE();
			privates[index] = PEEK(0);

			continue;
		}

		CASE_CODE(GET_PRIVATE) {
			PUSH(privates[READ_BYTE()]);
			continue;
		}

		CASE_CODE(SET_PRIVATE_LONG) {
			uint8_t index = READ_SHORT();
			privates[index] = PEEK(0);

			continue;
		}

		CASE_CODE(GET_PRIVATE_LONG) {
			PUSH(privates[READ_SHORT()]);
			continue;
		}

		CASE_CODE(SET_UPVALUE) {
			uint8_t index = READ_BYTE();
			*upvalues[index]->location = PEEK(0);

			continue;
		}

		CASE_CODE(GET_UPVALUE) {
			PUSH(*upvalues[READ_BYTE()]->location);
			continue;
		}

		CASE_CODE(JUMP_IF_FALSE) {
			uint16_t offset = READ_SHORT();

			if (lit_is_falsey(POP())) {
				ip += offset;
			}

			continue;
		}

		CASE_CODE(JUMP_IF_NULL) {
			uint16_t offset = READ_SHORT();

			if (IS_NULL(PEEK(0))) {
				ip += offset;
			}

			continue;
		}

		CASE_CODE(JUMP_IF_NULL_POPPING) {
			uint16_t offset = READ_SHORT();

			if (IS_NULL(POP())) {
				ip += offset;
			}

			continue;
		}

		CASE_CODE(JUMP) {
			uint16_t offset = READ_SHORT();
			ip += offset;

			continue;
		}

		CASE_CODE(JUMP_BACK) {
			uint16_t offset = READ_SHORT();
			ip -= offset;

			continue;
		}

		CASE_CODE(AND) {
			uint16_t offset = READ_SHORT();

			if (lit_is_falsey(PEEK(0))) {
				ip += offset;
			} else {
				DROP();
			}

			continue;
		}

		CASE_CODE(OR) {
			uint16_t offset = READ_SHORT();

			if (lit_is_falsey(PEEK(0))) {
				DROP();
			} else {
				ip += offset;
			}

			continue;
		}

		CASE_CODE(NULL_OR) {
			uint16_t offset = READ_SHORT();

			if (IS_NULL(PEEK(0))) {
				DROP();
			} else {
				ip += offset;
			}

			continue;
		}

		CASE_CODE(CALL) {
			uint8_t arg_count = READ_BYTE();

			WRITE_FRAME()
			CALL_VALUE(PEEK(arg_count), arg_count)

			continue;
		}

		CASE_CODE(CLOSURE) {
			LitFunction* function = AS_FUNCTION(READ_CONSTANT_LONG());
			LitClosure* closure = lit_create_closure(state, function);

			PUSH(OBJECT_VALUE(closure));

			for (uint i = 0; i < closure->upvalue_count; i++) {
				uint8_t is_local = READ_BYTE();
				uint8_t index = READ_BYTE();

				if (is_local) {
					closure->upvalues[i] = capture_upvalue(state, frame->slots + index);
				} else {
					closure->upvalues[i] = upvalues[index];
				}
			}

			continue;
		}

		CASE_CODE(CLOSE_UPVALUE) {
			close_upvalues(vm, fiber->stack_top - 1);
			DROP();

			continue;
		}

		CASE_CODE(CLASS) {
			LitString* name = READ_STRING_LONG();
			LitClass* klass = lit_create_class(state, name);

			PUSH(OBJECT_VALUE(klass));

			klass->super = state->object_class;

			lit_table_add_all(state, &klass->super->methods, &klass->methods);
			lit_table_add_all(state, &klass->super->static_fields, &klass->static_fields);

			lit_table_set(state, &vm->globals->values, name, OBJECT_VALUE(klass));

			continue;
		}

		CASE_CODE(GET_FIELD) {
			LitValue object = PEEK(1);

			if (IS_NULL(object)) {
				RUNTIME_ERROR("Attempt to index a null value")
			}

			LitValue value;
			LitString* name = AS_STRING(PEEK(0));

			if (IS_INSTANCE(object)) {
				LitInstance *instance = AS_INSTANCE(object);

				if (!lit_table_get(&instance->fields, name, &value)) {
					if (lit_table_get(&instance->klass->methods, name, &value)) {
						if (IS_FIELD(value)) {
							LitField* field = AS_FIELD(value);

							if (field->getter == NULL) {
								RUNTIME_ERROR_VARG("Class %s does not have a getter for the field %s", instance->klass->name->chars, name->chars)
							}

							DROP();
							WRITE_FRAME()
							CALL_VALUE(OBJECT_VALUE(AS_FIELD(value)->getter), 0)
							READ_FRAME()
							continue;
						} else {
							value = OBJECT_VALUE(lit_create_bound_method(state, OBJECT_VALUE(instance), value));
						}
					} else {
						value = NULL_VALUE;
					}
				}
			} else if (IS_CLASS(object)) {
				LitClass *klass = AS_CLASS(object);

				if (lit_table_get(&klass->static_fields, name, &value)) {
					if (IS_NATIVE_METHOD(value) || IS_PRIMITIVE_METHOD(value)) {
						value = OBJECT_VALUE(lit_create_bound_method(state, OBJECT_VALUE(klass), value));
					} else if (IS_FIELD(value)) {
						LitField* field = AS_FIELD(value);

						if (field->getter == NULL) {
							RUNTIME_ERROR_VARG("Class %s does not have a getter for the field %s", klass->name->chars, name->chars)
						}

						DROP();
						WRITE_FRAME()
						CALL_VALUE(OBJECT_VALUE(field->getter), 0)
						READ_FRAME()
						continue;
					}
				} else {
					value = NULL_VALUE;
				}
			} else {
				LitClass* klass = lit_get_class_for(state, object);

				if (klass == NULL) {
					RUNTIME_ERROR("Only instances and classes have fields")
				}

				if (lit_table_get(&klass->methods, name, &value)) {
					if (IS_FIELD(value)) {
						LitField *field = AS_FIELD(value);

						if (field->getter == NULL) {
							RUNTIME_ERROR_VARG("Class %s does not have a getter for the field %s", klass->name->chars, name->chars)
						}

						DROP();
						WRITE_FRAME()
						CALL_VALUE(OBJECT_VALUE(AS_FIELD(value)->getter), 0)
						READ_FRAME()
						continue;
					} else if (IS_NATIVE_METHOD(value) || IS_PRIMITIVE_METHOD(value)) {
						value = OBJECT_VALUE(lit_create_bound_method(state, object, value));
					}
				} else {
					value = NULL_VALUE;
				}
			}

			DROP(); // Pop field name
			fiber->stack_top[-1] = value;

			continue;
		}

		CASE_CODE(SET_FIELD) {
			LitValue instance = PEEK(2);

			if (IS_NULL(instance)) {
				RUNTIME_ERROR("Attempt to index a null value")
			}

			LitValue value = PEEK(1);
			LitString* field_name = AS_STRING(PEEK(0));

			if (IS_CLASS(instance)) {
				LitClass* klass = AS_CLASS(instance);
				LitValue setter;

				if (lit_table_get(&klass->static_fields, field_name, &setter) && IS_FIELD(setter)) {
					LitField* field = AS_FIELD(setter);

					if (field->setter == NULL) {
						RUNTIME_ERROR_VARG("Class %s does not have a setter for the field %s", klass->name->chars, field_name->chars)
					}

					DROP_MULTIPLE(2);
					PUSH(value);
					WRITE_FRAME()
					CALL_VALUE(OBJECT_VALUE(field->setter), 1)
					READ_FRAME()
					continue;
				}

				if (IS_NULL(value)) {
					lit_table_delete(&klass->static_fields, field_name);
				} else {
					lit_table_set(state, &klass->static_fields, field_name, value);
				}

				DROP_MULTIPLE(2); // Pop field name and the value
				fiber->stack_top[-1] = value;
			} else if (IS_INSTANCE(instance)) {
				LitInstance* inst = AS_INSTANCE(instance);
				LitValue setter;

				if (lit_table_get(&inst->klass->methods, field_name, &setter) && IS_FIELD(setter)) {
					LitField* field = AS_FIELD(setter);

					if (field->setter == NULL) {
						RUNTIME_ERROR_VARG("Class %s does not have a setter for the field %s", inst->klass->name->chars, field_name->chars)
					}

					DROP_MULTIPLE(2);
					PUSH(value);
					WRITE_FRAME()
					CALL_VALUE(OBJECT_VALUE(field->setter), 1)
					READ_FRAME()
					continue;
				}

				if (IS_NULL(value)) {
					lit_table_delete(&inst->fields, field_name);
				} else {
					lit_table_set(state, &inst->fields, field_name, value);
				}

				DROP_MULTIPLE(2); // Pop field name and the value
				fiber->stack_top[-1] = value;
			} else {
				LitClass* klass = lit_get_class_for(state, instance);

				if (klass == NULL) {
					RUNTIME_ERROR("Only instances and classes have fields")
				}

				LitValue setter;

				if (lit_table_get(&klass->methods, field_name, &setter) && IS_FIELD(setter)) {
					LitField* field = AS_FIELD(setter);

					if (field->setter == NULL) {
						RUNTIME_ERROR_VARG("Class %s does not have a setter for the field %s", klass->name->chars, field_name->chars)
					}

					DROP_MULTIPLE(2);
					PUSH(value);
					WRITE_FRAME()
					CALL_VALUE(OBJECT_VALUE(field->setter), 1)
					READ_FRAME()
					continue;
				} else {
					RUNTIME_ERROR_VARG("Class %s does not contain field %s", klass->name->chars, field_name->chars)
				}
			}

			continue;
		}

		CASE_CODE(SUBSCRIPT_GET) {
			INVOKE_METHOD(PEEK(1), "[]", 1)
			continue;
		}

		CASE_CODE(SUBSCRIPT_SET) {
			INVOKE_METHOD(PEEK(2), "[]", 2)
			continue;
		}

		CASE_CODE(PUSH_ARRAY_ELEMENT) {
			LitValues* values = &AS_ARRAY(PEEK(1))->values;
			int index = values->count;

			lit_values_ensure_size(state, values, index + 1);
			values->values[index] = PEEK(0);
			DROP();

			continue;
		}

		CASE_CODE(PUSH_MAP_ELEMENT) {
			lit_map_set(state, AS_MAP(PEEK(2)), AS_STRING(PEEK(1)), PEEK(0));
			DROP_MULTIPLE(2);

			continue;
		}

		CASE_CODE(STATIC_FIELD) {
			lit_table_set(state, &AS_CLASS(PEEK(1))->static_fields, READ_STRING_LONG(), PEEK(0));
			DROP();

			continue;
		}

		CASE_CODE(METHOD) {
			LitClass* klass = AS_CLASS(PEEK(1));
			LitString* name = READ_STRING_LONG();

			if ((klass->init_method == NULL || (klass->super != NULL && klass->init_method == ((LitClass*) klass->super)->init_method)) && name->length == 11 && memcmp(name->chars, "constructor", 11) == 0) {
				klass->init_method = AS_OBJECT(PEEK(0));
			}

			lit_table_set(state, &klass->methods, name, PEEK(0));
			DROP();

			continue;
		}

		CASE_CODE(DEFINE_FIELD) {
			lit_table_set(state, &AS_CLASS(PEEK(1))->methods, READ_STRING_LONG(), PEEK(0));
			DROP();

			continue;
		}

		CASE_CODE(INVOKE) {
			INVOKE_OPERATION(false)
			continue;
		}

		CASE_CODE(INVOKE_IGNORING) {
			INVOKE_OPERATION(true)
			continue;
		}

		CASE_CODE(INVOKE_SUPER) {
			uint8_t arg_count = READ_BYTE();
			LitString* method_name = READ_STRING_LONG();
			LitClass* klass = AS_INSTANCE(PEEK(arg_count))->klass->super;

			WRITE_FRAME()
			INVOKE_FROM_CLASS(klass, method_name, arg_count, true, methods, false)
			continue;
		}

		CASE_CODE(INVOKE_SUPER_IGNORING) {
			uint8_t arg_count = READ_BYTE();
			LitString* method_name = READ_STRING_LONG();
			LitClass* klass = AS_INSTANCE(PEEK(0))->klass->super;

			WRITE_FRAME()
			INVOKE_FROM_CLASS(klass, method_name, arg_count, true, methods, true)
			continue;
		}

		CASE_CODE(GET_SUPER_METHOD) {
			LitString* method_name = READ_STRING_LONG();
			LitValue instance = POP();

			LitValue value;

			if (lit_table_get(&((LitClass*) AS_INSTANCE(instance)->klass->super)->methods, method_name, &value)) {
				value = OBJECT_VALUE(lit_create_bound_method(state, instance, value));
			} else {
				value = NULL_VALUE;
			}

			PUSH(value);
			continue;
		}

		CASE_CODE(INHERIT) {
			LitValue super = PEEK(0);

			if (!IS_CLASS(super)) {
				RUNTIME_ERROR("Superclass must be a class")
			}

			LitClass* klass = AS_CLASS(PEEK(1));
			LitClass* super_klass = AS_CLASS(super);

			klass->super = super_klass;
			klass->init_method = super_klass->init_method;

			lit_table_add_all(state, &super_klass->methods, &klass->methods);
			lit_table_add_all(state, &klass->super->static_fields, &klass->static_fields);

			DROP();
			continue;
		}

		CASE_CODE(IS) {
			LitValue instance = PEEK(1);

			if (IS_NULL(instance)) {
				DROP_MULTIPLE(2);
				PUSH(FALSE_VALUE);

				continue;
			}

			LitClass* instance_klass = lit_get_class_for(state, instance);
			LitValue klass = PEEK(0);

			if (instance_klass == NULL || !IS_CLASS(klass)) {
				RUNTIME_ERROR("Operands must be an instance and a class")
			}

			LitClass* type = AS_CLASS(klass);
			bool found = false;

			while (instance_klass != NULL) {
				if (instance_klass == type) {
					found = true;
					break;
				}

				instance_klass = (LitClass*) instance_klass->super;
			}

			DROP_MULTIPLE(2); // Drop the instance and class
			PUSH(BOOL_VALUE(found));

			continue;
		}

		CASE_CODE(POP_LOCALS) {
			DROP_MULTIPLE(READ_SHORT());
			continue;
		}

		CASE_CODE(VARARG) {
			LitValue slot = slots[READ_BYTE()];

			if (!IS_ARRAY(slot)) {
				continue;
			}

			LitValues* values = &AS_ARRAY(slot)->values;
			lit_ensure_fiber_stack(state, fiber, values->count + frame->function->max_slots + (int) (fiber->stack_top - fiber->stack));

			for (uint i = 0; i < values->count; i++) {
				PUSH(values->values[i]);
			}

			// Hot-bytecode patching, increment the amount of arguments to OP_CALL
			ip[1] = ip[1] + 1;
			continue;
		}

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

#undef PUSH_GC
#undef POP_GC
#undef LIT_TRACE_FRAME