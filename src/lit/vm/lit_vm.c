#include <lit/vm/lit_vm.h>
#include <lit/vm/lit_object.h>
#include <lit/debug/lit_debug.h>
#include <lit/mem/lit_mem.h>
#include <lit/util/lit_fs.h>

#include <stdio.h>
#include <math.h>
#include <string.h>

static void reset_stack(LitVm* vm) {
	if (vm->fiber != NULL) {
		vm->fiber->stack_top = vm->fiber->stack;
	}
}

void lit_init_vm(LitState* state, LitVm* vm) {
	vm->state = state;
	vm->objects = NULL;
	vm->fiber = NULL;
	vm->fiber_updated = false;
	vm->open_upvalues = NULL;

	vm->gray_stack = NULL;
	vm->gray_count = 0;
	vm->gray_capacity = 0;

	lit_init_table(&vm->strings);
	lit_init_table(&vm->globals);
	lit_init_table(&vm->modules);
}

void lit_free_vm(LitVm* vm) {
	lit_free_table(vm->state, &vm->strings);
	lit_free_table(vm->state, &vm->globals);
	lit_free_table(vm->state, &vm->modules);
	lit_free_objects(vm->state, vm->objects);

	lit_init_vm(vm->state, vm);
}

void lit_push(LitVm* vm, LitValue value) {
	*vm->fiber->stack_top = value;
	vm->fiber->stack_top++;
}

LitValue lit_pop(LitVm* vm) {
	assert(vm->fiber->stack_top > vm->fiber->stack);
	vm->fiber->stack_top--;
	return *vm->fiber->stack_top;
}

static void trace_stack(LitVm* vm) {
	if (vm->fiber->stack_top == vm->fiber->stack) {
		return;
	}

	printf("        | ");

	for (LitValue* slot = vm->fiber->stack; slot < vm->fiber->stack_top; slot++) {
		printf("[ ");
		lit_print_value(*slot);
		printf(" ]");
	}

	printf("\n");
}

void lit_handle_runtime_error(LitVm* vm, LitString* error_string) {
	LitValue error = OBJECT_VALUE(error_string);
	LitFiber* fiber = vm->fiber;

	while (fiber != NULL) {
		fiber->error = error;

		if (fiber->try) {
			lit_push(vm, error);

			vm->fiber = fiber->parent;
			vm->fiber_updated = true;

			return;
		}

		LitFiber* caller = fiber->parent;
		fiber->parent = NULL;
		fiber = caller;
	}

	fiber = vm->fiber;
	fiber->abort = true;

	fprintf(stderr, "%s\n", error_string->chars);

	for (int i = (int) fiber->frame_count - 1; i >= 0; i--) {
		LitCallFrame* frame = &fiber->frames[i];
		LitFunction* function = frame->function;
		LitString* name = function->name;

		fprintf(stderr, "[line %d] in %s()\n", lit_chunk_get_line(&function->chunk, frame->ip - function->chunk.code - 1), name == NULL ? "unknown" : name->chars);
	}

	reset_stack(vm);
}

void lit_runtime_error(LitVm* vm, const char* format, ...) {
	LitFiber* fiber = vm->fiber;

	va_list args;
	va_start(args, format);
	size_t buffer_size = vsnprintf(NULL, 0, format, args) + 1;
	va_end(args);

	char buffer[buffer_size];

	va_start(args, format);
	vsnprintf(buffer, buffer_size, format, args);
	va_end(args);

	lit_handle_runtime_error(vm, lit_copy_string(vm->state, buffer, buffer_size));
}

static inline bool is_falsey(LitValue value) {
	if (IS_NUMBER(value)) {
		return AS_NUMBER(value) == 0;
	}

	return IS_NULL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static bool call(LitVm* vm, LitFunction* function, LitClosure* closure, uint8_t arg_count) {
	LitFiber* fiber = vm->fiber;

	if (fiber->frame_count == LIT_CALL_FRAMES_MAX) {
		lit_runtime_error(vm, "Stack overflow");
		return false;
	}

	LitCallFrame* frame = &fiber->frames[fiber->frame_count++];

	frame->function = function;
	frame->closure = closure;
	frame->ip = function->chunk.code;
	frame->slots = fiber->stack_top - arg_count - 1;

	uint function_arg_count = function->arg_count;

	if (arg_count < function_arg_count) {
		for (uint i = 0; i < function_arg_count - arg_count; i++) {
			lit_push(vm, NULL_VALUE);
		}
	} else if (arg_count > function_arg_count) {
		for (uint i = 0; i < arg_count - function_arg_count; i++) {
			lit_pop(vm);
		}
	}

#ifdef LIT_TRACE_EXECUTION
	printf("== f%i %s ==\n", fiber->frame_count - 1, frame->function->name->chars);
#endif

	return true;
}

static bool call_value(LitVm* vm, LitValue callee, uint8_t arg_count) {
	if (IS_OBJECT(callee)) {
		switch (OBJECT_TYPE(callee)) {
			case OBJECT_FUNCTION: {
				return call(vm, AS_FUNCTION(callee), NULL, arg_count);
			}

			case OBJECT_CLOSURE: {
				LitClosure* closure = AS_CLOSURE(callee);
				return call(vm, closure->function, closure, arg_count);
			}

			case OBJECT_NATIVE_FUNCTION: {
				LitNativeFunctionFn native = AS_NATIVE_FUNCTION(callee)->function;
				LitValue result = native(vm, arg_count, vm->fiber->stack_top - arg_count);
				vm->fiber->stack_top -= arg_count + 1;
				lit_push(vm, result);

				return !vm->fiber->abort;
			}

			case OBJECT_NATIVE_METHOD: {
				LitNativeMethod* method = AS_NATIVE_METHOD(callee);

				LitFiber* fiber = vm->fiber;
				LitValue result = method->method(vm, *(vm->fiber->stack_top - arg_count - 1), arg_count, vm->fiber->stack_top - arg_count);

				if (!vm->fiber_updated) {
					vm->fiber->stack_top -= arg_count + 1;
					lit_push(vm, result);
				}

				return !vm->fiber->abort;
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

				return true;
			}

			case OBJECT_BOUND_METHOD: {
				LitBoundMethod* bound_method = AS_BOUND_METHOD(callee);
				vm->fiber->stack_top[-arg_count - 1] = bound_method->receiver;

				return call(vm, bound_method->method, NULL, arg_count);
			}

			case OBJECT_NATIVE_BOUND_METHOD: {
				LitNativeBoundMethod* bound_method = AS_NATIVE_BOUND_METHOD(callee);

				LitValue result = bound_method->method->method(vm, bound_method->receiver, arg_count, vm->fiber->stack_top - arg_count);
				vm->fiber->stack_top -= arg_count + 1;
				lit_push(vm, result);

				return !vm->fiber->abort;
			}

			default: {
				break;
			}
		}
	}

	if (IS_NULL(callee)) {
		lit_runtime_error(vm, "Attempt to call a null value");
	} else {
		if (IS_OBJECT(callee)) {
			LitObjectType type = OBJECT_TYPE(callee);
			printf("object\n");
		} else if (IS_NUMBER(callee)) {
			printf("number\n");
		} else if (IS_BOOL(callee)) {
			printf("bool\n");
		}

		lit_runtime_error(vm, "Can only call functions and classes");
	}

	return false;
}

static LitUpvalue* capture_upvalue(LitState* state, LitValue* local) {
	LitUpvalue* previous_upvalue = NULL;
	LitUpvalue* upvalue = state->vm->open_upvalues;

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
		state->vm->open_upvalues = created_upvalue;
	} else {
		previous_upvalue->next = created_upvalue;
	}

	return created_upvalue;
}

static void close_upvalues(LitVm* vm, const LitValue* last) {
	while (vm->open_upvalues != NULL && vm->open_upvalues->location >= last) {
		LitUpvalue* upvalue = vm->open_upvalues;

		upvalue->closed = *upvalue->location;
		upvalue->location = &upvalue->closed;

		vm->open_upvalues = upvalue->next;
	}
}

static bool invoke_from_class(LitVm* vm, LitClass* klass, LitString* method_name, uint8_t arg_count, bool error) {
	LitValue method;

	if (!lit_table_get(&klass->methods, method_name, &method)) {
		if (error) {
			lit_runtime_error(vm, "Attempt to call method '%s', that is not defined in class %s", method_name->chars, klass->name->chars);
		}

		return false;
	}

	if (IS_NATIVE_METHOD(method)) {
		return call_value(vm, method, arg_count);
	}

	return call(vm, AS_FUNCTION(method), NULL, arg_count);
}

static bool invoke_static_from_class(LitVm* vm, LitClass* klass, LitString* method_name, uint8_t arg_count) {
	LitValue method;

	if (!lit_table_get(&klass->static_fields, method_name, &method)) {
		lit_runtime_error(vm, "Attempt to call undefined static method '%s'", method_name->chars);
		return false;
	}

	if (IS_NATIVE_METHOD(method)) {
		return call_value(vm, method, arg_count);
	}

	return call(vm, AS_FUNCTION(method), NULL, arg_count);
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

	vm->fiber = fiber;
	vm->fiber_updated = false;

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
	privates = fiber->module->privates;	\
	upvalues = frame->closure == NULL ? NULL : frame->closure->upvalues;

#define WRITE_FRAME() frame->ip = ip;
#define RETURN_ERROR() return (LitInterpretResult) {INTERPRET_RUNTIME_ERROR, NULL_VALUE};

#define INVOKE_METHOD(instance, method_name, arg_count) \
	LitClass* klass = lit_get_class_for(state, instance); \
	if (klass == NULL) { \
		lit_runtime_error(vm, "Only instances and classes have methods"); \
		RETURN_ERROR() \
	} \
	WRITE_FRAME(); \
	if (!invoke_from_class(vm, klass, CONST_STRING(state, method_name), arg_count, true)) { \
		RETURN_ERROR() \
	} \
	READ_FRAME();

#define BINARY_OP(type, op, op_string) \
	LitValue a = PEEK(1); \
	LitValue b = PEEK(0); \
	if (IS_NUMBER(a)) { \
		if (!IS_NUMBER(b)) { \
			lit_runtime_error(vm, "Attempt to use operator %s with a number and not a number", op_string); \
			RETURN_ERROR() \
		} \
		DROP(); \
		*(fiber->stack_top - 1) = (type(AS_NUMBER(a) op AS_NUMBER(b))); \
		continue; \
	} \
	if (IS_NULL(a)) { \
		lit_runtime_error(vm, "Attempt to use operator %s on a null value", op_string); \
		RETURN_ERROR() \
	} \
	INVOKE_METHOD(a, op_string, 1)


#define BITWISE_OP(op, op_string) \
	LitValue a = PEEK(1); \
	LitValue b = PEEK(0); \
	if (!IS_NUMBER(a) || !IS_NUMBER(b)) { \
		lit_runtime_error(vm, "Operands of bitwise operator %s must be two numbers", op_string); \
		RETURN_ERROR() \
	} \
	DROP(); \
	*(fiber->stack_top - 1) = (NUMBER_VALUE((int) AS_NUMBER(a) op (int) AS_NUMBER(b)));

#ifdef LIT_TRACE_EXECUTION
	printf("== f%i %s ==\n", fiber->frame_count - 1, frame->function->name->chars);
	uint8_t instruction;
#endif

	while (true) {
#ifdef LIT_TRACE_STACK
		trace_stack(vm);
#endif

#ifdef LIT_TRACE_EXECUTION
		instruction = *ip++;
		lit_disassemble_instruction(current_chunk, (uint) (ip - current_chunk->code - 1));
		goto *dispatch_table[instruction];
#else
		goto *dispatch_table[*ip++];
#endif

		CASE_CODE(POP) {
			DROP();
			continue;
		}

		CASE_CODE(POP_MULTIPLE) {
			DROP_MULTIPLE(READ_BYTE());
			continue;
		}

		CASE_CODE(RETURN) {
			LitValue result = POP();
			close_upvalues(vm, slots);

			WRITE_FRAME()
			fiber->frame_count--;

			if (fiber->frame_count == 0) {
				DROP();

				#ifdef LIT_TRACE_EXECUTION
					printf("== end ==\n");
				#endif

				return (LitInterpretResult) { INTERPRET_OK, result };
			}

			fiber->stack_top = frame->slots;
			PUSH(result);
			READ_FRAME()

			#ifdef LIT_TRACE_EXECUTION
				printf("== f%i %s ==\n", fiber->frame_count - 1, frame->function->name->chars);
			#endif

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
				lit_runtime_error(vm, "Range operands must be number");
				RETURN_ERROR()
			}

			PUSH(OBJECT_VALUE(lit_create_range(state, AS_NUMBER(a), AS_NUMBER(b))));
			continue;
		}

		CASE_CODE(NEGATE) {
			if (!IS_NUMBER(PEEK(0))) {
				lit_runtime_error(vm, "Operand must be a number");
				RETURN_ERROR()
			}

			PUSH(NUMBER_VALUE(-AS_NUMBER(POP())));
			continue;
		}

		CASE_CODE(NOT) {
			if (IS_INSTANCE(PEEK(0))) {
				WRITE_FRAME()

				if (invoke_from_class(vm, AS_INSTANCE(PEEK(0))->klass, CONST_STRING(state, "!"), 0, false)) {
					READ_FRAME()
					continue;
				}
			}

			PUSH(BOOL_VALUE(is_falsey(POP())));
			continue;
		}

		CASE_CODE(BNOT) {
			if (!IS_NUMBER(PEEK(0))) {
				lit_runtime_error(vm, "Operand must be a number");
				RETURN_ERROR()
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

				if (invoke_from_class(vm, AS_INSTANCE(PEEK(1))->klass, CONST_STRING(state, "=="), 1, false)) {
					READ_FRAME()
					continue;
				}
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
			lit_table_set(state, &vm->globals, name, PEEK(0));

			continue;
		}

		CASE_CODE(GET_GLOBAL) {
			LitString* name = READ_STRING_LONG();
			LitValue value;

			if (!lit_table_get(&vm->globals, name, &value)) {
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

			if (is_falsey(PEEK(0))) {
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

		CASE_CODE(JUMP) {
			ip += READ_SHORT();
			continue;
		}

		CASE_CODE(JUMP_BACK) {
			ip -= READ_SHORT();
			continue;
		}

		CASE_CODE(CALL) {
			uint8_t arg_count = READ_BYTE();
			WRITE_FRAME()

			if (!call_value(vm, PEEK(arg_count), arg_count)) {
				RETURN_ERROR()
			}

			READ_FRAME()
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
			LitString* name = AS_STRING(PEEK(0));
			LitClass* klass = lit_create_class(state, name);

			klass->super = state->object_class;
			lit_table_add_all(state, &klass->super->methods, &klass->methods);
			lit_table_add_all(state, &klass->super->static_fields, &klass->static_fields);

			DROP(); // Pop the class name

			lit_push_root(state, (LitObject *) klass);
			lit_table_set(state, &vm->globals, name, OBJECT_VALUE(klass));
			lit_pop_root(state);

			PUSH(OBJECT_VALUE(klass));

			continue;
		}

		CASE_CODE(GET_FIELD) {
			LitValue object = PEEK(1);

			if (IS_NULL(object)) {
				lit_runtime_error(vm, "Attempt to index a null value");
				RETURN_ERROR()
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
								lit_runtime_error(vm, "Class %s does not have a getter for the field %s", instance->klass->name->chars, name->chars);
								RETURN_ERROR()
							}

							DROP();
							WRITE_FRAME()

							if (!call_value(vm, OBJECT_VALUE(AS_FIELD(value)->getter), 0)) {
								RETURN_ERROR()
							}

							READ_FRAME()
							continue;
						} else {
							value = OBJECT_VALUE(lit_create_bound_method(state, OBJECT_VALUE(instance), AS_FUNCTION(value)));
						}
					} else {
						value = NULL_VALUE;
					}
				}
			} else if (IS_CLASS(object)) {
				LitClass *klass = AS_CLASS(object);

				if (lit_table_get(&klass->static_fields, name, &value)) {
					if (IS_NATIVE_METHOD(value)) {
						value = OBJECT_VALUE(lit_create_native_bound_method(state, OBJECT_VALUE(klass), AS_NATIVE_METHOD(value)));
					} else if (IS_FUNCTION(value)) {
						value = OBJECT_VALUE(lit_create_bound_method(state, OBJECT_VALUE(klass), AS_FUNCTION(value)));
					} else if (IS_FIELD(value)) {
						LitField* field = AS_FIELD(value);

						if (field->getter == NULL) {
							lit_runtime_error(vm, "Class %s does not have a getter for the field %s", klass->name->chars, name->chars);
							RETURN_ERROR()
						}

						DROP();
						WRITE_FRAME()

						if (!call_value(vm, OBJECT_VALUE(field->getter), 0)) {
							RETURN_ERROR()
						}

						READ_FRAME()
						continue;
					}
				} else {
					value = NULL_VALUE;
				}
			} else {
				LitClass* klass = lit_get_class_for(state, object);

				if (klass == NULL) {
					lit_runtime_error(vm, "Only instances and classes have fields");
					RETURN_ERROR()
				}

				if (lit_table_get(&klass->methods, name, &value)) {
					if (IS_FIELD(value)) {
						LitField *field = AS_FIELD(value);

						if (field->getter == NULL) {
							lit_runtime_error(vm, "Class %s does not have a getter for the field %s", klass->name->chars, name->chars);
							RETURN_ERROR()
						}

						DROP();
						WRITE_FRAME()

						if (!call_value(vm, OBJECT_VALUE(AS_FIELD(value)->getter), 0)) {
							RETURN_ERROR()
						}

						READ_FRAME()
						continue;
					} else if (IS_NATIVE_METHOD(value)) {
						value = OBJECT_VALUE(lit_create_native_bound_method(state, object, AS_NATIVE_METHOD(value)));
					} else {
						value = OBJECT_VALUE(lit_create_bound_method(state, object, AS_FUNCTION(value)));
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
				lit_runtime_error(vm, "Attempt to index a null value");
				RETURN_ERROR()
			}

			LitValue value = PEEK(1);
			LitString* field_name = AS_STRING(PEEK(0));

			if (IS_CLASS(instance)) {
				LitClass* klass = AS_CLASS(instance);
				LitValue setter;

				if (lit_table_get(&klass->static_fields, field_name, &setter) && IS_FIELD(setter)) {
					LitField* field = AS_FIELD(setter);

					if (field->setter == NULL) {
						lit_runtime_error(vm, "Class %s does not have a setter for the field %s", klass->name->chars, field_name->chars);
						RETURN_ERROR()
					}

					DROP_MULTIPLE(2);
					PUSH(value);
					WRITE_FRAME()

					if (!call_value(vm, OBJECT_VALUE(field->setter), 1)) {
						RETURN_ERROR()
					}

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
						lit_runtime_error(vm, "Class %s does not have a setter for the field %s", inst->klass->name->chars, field_name->chars);
						RETURN_ERROR()
					}

					DROP_MULTIPLE(2);
					PUSH(value);
					WRITE_FRAME()

					if (!call_value(vm, OBJECT_VALUE(field->setter), 1)) {
						RETURN_ERROR()
					}

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
					lit_runtime_error(vm, "Only instances and classes have fields");
					RETURN_ERROR()
				}

				LitValue setter;

				if (lit_table_get(&klass->methods, field_name, &setter) && IS_FIELD(setter)) {
					LitField* field = AS_FIELD(setter);

					if (field->setter == NULL) {
						lit_runtime_error(vm, "Class %s does not have a setter for the field %s", klass->name->chars, field_name->chars);
						RETURN_ERROR()
					}

					DROP_MULTIPLE(2);
					PUSH(value);
					WRITE_FRAME()

					if (!call_value(vm, OBJECT_VALUE(field->setter), 1)) {
						RETURN_ERROR()
					}

					READ_FRAME()
					continue;
				} else {
					lit_runtime_error(vm, "Class %s does not contain field %s", klass->name->chars, field_name->chars);
					RETURN_ERROR()
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
			LitString* method_name = READ_STRING_LONG();
			uint8_t arg_count = READ_BYTE();
			LitValue receiver = PEEK(arg_count);

			if (IS_NULL(receiver)) {
				lit_runtime_error(vm, "Attempt to index a null value");
				RETURN_ERROR()
			}

			WRITE_FRAME()

			if (IS_CLASS(receiver)) {
				if (!invoke_static_from_class(vm, AS_CLASS(receiver), method_name, arg_count)) {
					RETURN_ERROR()
				}

				if (vm->fiber_updated) {
					vm->fiber_updated = false;

					WRITE_FRAME()

					#ifdef LIT_TRACE_EXECUTION
						int frame_id = vm->fiber->frame_count - 1;
						printf("== f%i %s ==\n", frame_id, vm->fiber->frames[frame_id].function->name->chars);
					#endif

					LitValue result = fiber->stack_top[-arg_count - 1];
					fiber->stack_top -= arg_count + 1;

					return (LitInterpretResult) { INTERPRET_OK, result };
				}

				READ_FRAME()
				continue;
			} else if (IS_INSTANCE(receiver)) {
				LitInstance* instance = AS_INSTANCE(receiver);
				LitValue value;

				if (lit_table_get(&instance->fields, method_name, &value)) {
					fiber->stack_top[-arg_count - 1] = value;

					if (!call_value(vm, value, arg_count)) {
						RETURN_ERROR()
					}

					READ_FRAME()
					continue;
				}

				if (!invoke_from_class(vm, instance->klass, method_name, arg_count, true)) {
					RETURN_ERROR()
				}

				READ_FRAME()
			} else {
				LitClass* type = lit_get_class_for(state, receiver);

				if (type == NULL) {
					lit_runtime_error(vm, "Only instances and classes have methods");
					RETURN_ERROR()
				}

				if (!invoke_from_class(vm, type, method_name, arg_count, true)) {
					RETURN_ERROR()
				}

				READ_FRAME()
			}

			continue;
		}

		CASE_CODE(INVOKE_SUPER) {
			LitString* method_name = READ_STRING_LONG();
			uint8_t arg_count = READ_BYTE();
			LitClass* klass = AS_CLASS(POP());

			WRITE_FRAME()

			if (!invoke_from_class(vm, klass, method_name, arg_count, true)) {
				RETURN_ERROR()
			}

			READ_FRAME()
			continue;
		}

		CASE_CODE(GET_SUPER_METHOD) {
			LitString* method_name = READ_STRING_LONG();
			LitValue instance = POP();

			LitValue value;

			if (lit_table_get(&((LitClass*) AS_INSTANCE(instance)->klass->super)->methods, method_name, &value)) {
				value = OBJECT_VALUE(lit_create_bound_method(state, instance, AS_FUNCTION(value)));
			} else {
				value = NULL_VALUE;
			}

			PUSH(value);
			continue;
		}

		CASE_CODE(INHERIT) {
			LitValue super = PEEK(0);

			if (!IS_CLASS(super)) {
				lit_runtime_error(vm, "Superclass must be a class");
				RETURN_ERROR()
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
				lit_runtime_error(vm, "Operands must be an instance and a class");
				RETURN_ERROR()
			}

			LitClass* type = AS_CLASS(klass);
			bool found = false;

			while (instance_klass != NULL) {
				if (instance_klass == type) {
					found = true;
					break;
				}

				instance_klass = (LitClass *) instance_klass->super;
			}

			DROP_MULTIPLE(2); // Drop the instance and class
			PUSH(BOOL_VALUE(found));

			continue;
		}

		lit_runtime_error(vm, "Unknown op code '%d'", *ip);
		break;
	}

#undef INVOKE_METHOD
#undef DROP_MULTIPLE
#undef PUSH
#undef DROP
#undef POP
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
