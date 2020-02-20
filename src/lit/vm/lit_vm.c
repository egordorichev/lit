#include <lit/vm/lit_vm.h>
#include <lit/vm/lit_object.h>
#include <lit/debug/lit_debug.h>
#include <lit/mem/lit_mem.h>
#include <lit/util/lit_fs.h>
#include <lit/api/lit_api.h>

#include <stdio.h>
#include <math.h>
#include <string.h>
#include <time.h>

static void reset_stack(LitVm* vm) {
	if (vm->fiber != NULL) {
		vm->fiber->stack_top = vm->fiber->stack;
	}
}

LIT_NATIVE(time) {
	return NUMBER_VALUE((double) clock() / CLOCKS_PER_SEC);
}

LIT_NATIVE(print) {
	for (uint i = 0; i < arg_count; i++) {
		lit_print_value(args[i]);
		printf("\n");
	}

	return NULL_VALUE;
}

void lit_define_std(LitState* state) {
	lit_define_native(state, "time", time_native);
	lit_define_native(state, "print", print_native);
}

void lit_init_vm(LitState* state, LitVm* vm) {
	vm->state = state;
	vm->objects = NULL;
	vm->fiber = NULL;
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

static void runtime_error(LitVm* vm, const char* format, ...) {
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	fputs("\n", stderr);

	for (int i = (int) vm->fiber->frame_count - 1; i >= 0; i--) {
		LitCallFrame* frame = &vm->fiber->frames[i];
		LitFunction* function = frame->function;
		LitString* name = function->name;

		fprintf(stderr, "[line %d] in %s()\n", lit_chunk_get_line(&function->chunk, frame->ip - function->chunk.code - 1), name == NULL ? "unknown" : name->chars);
	}

	reset_stack(vm);
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
		runtime_error(vm, "Stack overflow");
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
	printf("== %s ==\n", frame->function->name->chars);
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

			case OBJECT_NATIVE: {
				LitNativeFn native = AS_NATIVE(callee);
				LitValue result = native(vm, arg_count, vm->fiber->stack_top - arg_count);
				vm->fiber->stack_top -= arg_count + 1;
				lit_push(vm, result);

				return true;
			}

			case OBJECT_CLASS: {
				LitClass* klass = AS_CLASS(callee);
				vm->fiber->stack_top[-arg_count - 1] = OBJECT_VALUE(lit_create_instance(vm->state, klass));

				if (klass->init_method != NULL) {
					return call(vm, klass->init_method, NULL, arg_count);
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

			default: {
				break;
			}
		}
	}

	if (IS_NULL(callee)) {
		runtime_error(vm, "Attempt to call a null value");
	} else {
		runtime_error(vm, "Can only call functions and classes");
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

static bool invoke_from_class(LitVm* vm, LitClass* klass, LitString* method_name, uint8_t arg_count) {
	LitValue method;

	if (!lit_table_get(&klass->methods, method_name, &method)) {
		runtime_error(vm, "Attempt to call undefined method '%s'", method_name->chars);
		return false;
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

	register LitCallFrame* frame = &fiber->frames[fiber->frame_count - 1];
	register LitChunk* current_chunk = &frame->function->chunk;
	register uint8_t* ip = frame->ip = current_chunk->code;
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
	upvalues = frame->closure == NULL ? NULL : frame->closure->upvalues;

#define WRITE_FRAME() frame->ip = ip;
#define RETURN_ERROR() return (LitInterpretResult) {INTERPRET_RUNTIME_ERROR, NULL_VALUE};

#define BINARY_OP(type, op) \
    do { \
			if (!IS_NUMBER(PEEK(0)) || !IS_NUMBER(PEEK(1))) { \
				runtime_error(vm, "Operands must be numbers"); \
				RETURN_ERROR() \
			} \
      double b = AS_NUMBER(POP()); \
      double a = AS_NUMBER(POP()); \
      PUSH(type(a op b)); \
    } while (false);

#ifdef LIT_TRACE_EXECUTION
	printf("== %s ==\n", frame->function->name->chars);
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
				return (LitInterpretResult) { INTERPRET_OK, result };
			}

			fiber->stack_top = frame->slots;
			PUSH(result);
			READ_FRAME()

			#ifdef LIT_TRACE_EXECUTION
				printf("== %s ==\n", frame->function->name->chars);
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

		CASE_CODE(NEGATE) {
			if (!IS_NUMBER(PEEK(0))) {
				runtime_error(vm, "Operand must be a number");
				RETURN_ERROR()
			}

			PUSH(NUMBER_VALUE(-AS_NUMBER(POP())));
			continue;
		}

		CASE_CODE(NOT) {
			PUSH(BOOL_VALUE(is_falsey(POP())));
			continue;
		}

		CASE_CODE(ADD) {
			BINARY_OP(NUMBER_VALUE, +)
			continue;
		}

		CASE_CODE(SUBTRACT) {
			BINARY_OP(NUMBER_VALUE, -)
			continue;
		}

		CASE_CODE(MULTIPLY) {
			BINARY_OP(NUMBER_VALUE, *)
			continue;
		}

		CASE_CODE(DIVIDE) {
			BINARY_OP(NUMBER_VALUE, /)
			continue;
		}

		CASE_CODE(MOD) {
			if (!IS_NUMBER(PEEK(0)) || !IS_NUMBER(PEEK(1))) {
				runtime_error(vm, "Operands must be numbers");
				RETURN_ERROR()
			}

      double b = AS_NUMBER(POP());
      double a = AS_NUMBER(POP());

      PUSH(NUMBER_VALUE(fmod(a, b)));
      continue;
		}

		CASE_CODE(EQUAL) {
			LitValue a = POP();
			LitValue b = POP();

			PUSH(BOOL_VALUE(a == b));
			continue;
		}

		CASE_CODE(NOT_EQUAL) {
			LitValue a = POP();
			LitValue b = POP();

			PUSH(BOOL_VALUE(a != b));
			continue;
		}

		CASE_CODE(GREATER) {
			BINARY_OP(BOOL_VALUE, >)
			continue;
		}

		CASE_CODE(GREATER_EQUAL) {
			BINARY_OP(BOOL_VALUE, >=)
			continue;
		}

		CASE_CODE(LESS) {
			BINARY_OP(BOOL_VALUE, <)
			continue;
		}

		CASE_CODE(LESS_EQUAL) {
			BINARY_OP(BOOL_VALUE, <=)
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

		CASE_CODE(REQUIRE) {
			LitValue name = POP();

			if (!IS_STRING(name)) {
				runtime_error(vm, "require() argument must be a string");
				RETURN_ERROR()
			}

			const char* path = AS_STRING(name)->chars;
			size_t length = strlen(path);
			char full_path[length + 5];

			memcpy((void *) full_path, path, length);
			memcpy((void *) (full_path + length), ".lit\0", length);

			for (uint i = 0; i < length; i++) {
				if (full_path[i] == '.' || full_path[i] == '\\') {
					full_path[i] = '/';
				}
			}

			LitString* module_name = lit_copy_string(state, full_path, length);
			LitValue existing_module;

			if (lit_table_get(&vm->modules, module_name, &existing_module)) {
				PUSH(AS_MODULE(existing_module)->return_value);
				continue;
			}

			const char* source = lit_read_file(full_path);

			if (source == NULL) {
				runtime_error(vm, "Failed to open '%s'", full_path);
				RETURN_ERROR()
			}

			PUSH(lit_internal_interpret(state, module_name, source).result);

			#ifdef LIT_TRACE_EXECUTION
				printf("== %s ==\n", frame->function->name->chars);
			#endif

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
			DROP(); // Pop the class name

			lit_push_root(state, (LitObject *) klass);
			lit_table_set(state, &vm->globals, name, OBJECT_VALUE(klass));
			lit_pop_root(state);

			PUSH(OBJECT_VALUE(klass));

			continue;
		}

		CASE_CODE(GET_FIELD) {
			if (!IS_INSTANCE(PEEK(1))) {
				runtime_error(vm, "Only instances have fields");
				RETURN_ERROR()
			}

			LitValue value;
			LitInstance* instance = AS_INSTANCE(PEEK(1));
			LitString* name = AS_STRING(PEEK(0));

			if (!lit_table_get(&instance->fields, name, &value)) {
				if (lit_table_get(&instance->klass->methods, name, &value)) {
					value = OBJECT_VALUE(lit_create_bound_method(state, OBJECT_VALUE(instance), AS_FUNCTION(value)));
				} else {
					value = NULL_VALUE;
				}
			}

			DROP(); // Pop field name
			fiber->stack_top[-1] = value;

			continue;
		}

		CASE_CODE(SET_FIELD) {
			if (!IS_INSTANCE(PEEK(2))) {
				runtime_error(vm, "Only instances have fields");
				RETURN_ERROR()
			}

			LitValue value = PEEK(1);

			if (IS_NULL(value)) {
				lit_table_delete(&AS_INSTANCE(PEEK(2))->fields, AS_STRING(PEEK(0)));
			} else {
				lit_table_set(state, &AS_INSTANCE(PEEK(2))->fields, AS_STRING(PEEK(0)), value);
			}

			DROP_MULTIPLE(2); // Pop field name and the value
			fiber->stack_top[-1] = value;

			continue;
		}

		CASE_CODE(SUBSCRIPT_GET) {
			if (!IS_ARRAY(PEEK(1))) {
				runtime_error(vm, "Only arrays can be indexed");
				RETURN_ERROR()
			}

			if (!IS_NUMBER(PEEK(0))) {
				runtime_error(vm, "Array index must be a number");
				RETURN_ERROR()
			}

			LitValues *values = &AS_ARRAY(PEEK(1))->values;
			int index = AS_NUMBER(PEEK(0));

			if (index < 0) {
				index = fmax(0, values->count + index);
			}

			DROP_MULTIPLE(2);

			if (values->capacity <= index) {
				PUSH(NULL_VALUE);
			} else {
				PUSH(values->values[index]);
			}

			continue;
		}

		CASE_CODE(SUBSCRIPT_SET) {
			if (!IS_ARRAY(PEEK(2))) {
				runtime_error(vm, "Only arrays can be indexed");
				RETURN_ERROR()
			}

			if (!IS_NUMBER(PEEK(1))) {
				runtime_error(vm, "Array index must be a number");
				RETURN_ERROR()
			}

			LitValues *values = &AS_ARRAY(PEEK(2))->values;
			int index = AS_NUMBER(PEEK(1));

			if (index < 0) {
				index = fmax(0, values->count + index);
			}

			lit_values_ensure_size(state, values, index + 1);
			LitValue value = values->values[index] = PEEK(0);
			DROP_MULTIPLE(2);

			*fiber->stack_top = value;
			continue;
		}

		CASE_CODE(PUSH_ELEMENT) {
			if (!IS_ARRAY(PEEK(1))) {
				runtime_error(vm, "Only arrays can be indexed");
				RETURN_ERROR()
			}

			LitValues *values = &AS_ARRAY(PEEK(1))->values;
			int index = values->count;

			lit_values_ensure_size(state, values, index + 1);
			LitValue value = values->values[index] = PEEK(0);
			DROP();

			continue;
		}

		CASE_CODE(METHOD) {
			LitClass* klass = AS_CLASS(PEEK(1));
			LitString* name = READ_STRING_LONG();

			if (klass->init_method == NULL && name->length == 11 && memcmp(name->chars, "constructor", 11) == 0) {
				klass->init_method = AS_FUNCTION(PEEK(0));
			}

			lit_table_set(state, &klass->methods, name, PEEK(0));
			DROP();

			continue;
		}

		CASE_CODE(INVOKE) {
			LitString* method_name = READ_STRING_LONG();
			uint8_t arg_count = READ_BYTE();

			WRITE_FRAME()

			LitValue receiver = PEEK(arg_count);

			if (!IS_INSTANCE(receiver)) {
				runtime_error(vm, "Only instances have methods");
				RETURN_ERROR()
			}

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

			if (!invoke_from_class(vm, instance->klass, method_name, arg_count)) {
				RETURN_ERROR()
			}

			READ_FRAME()
			continue;
		}

		CASE_CODE(INHERIT) {
			LitValue super = PEEK(0);

			if (!IS_CLASS(super)) {
				runtime_error(vm, "Superclass must be a class");
				RETURN_ERROR()
			}

			LitClass* klass = AS_CLASS(PEEK(1));
			lit_table_add_all(state, &AS_CLASS(super)->methods, &klass->methods);

			DROP();
			continue;
		}

		runtime_error(vm, "Unknown op code '%d'", *ip);
		break;
	}

#undef POP_MULTIPLE
#undef PUSH
#undef POP
#undef WRITE_FRAME
#undef READ_FRAME
#undef PEEK
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
