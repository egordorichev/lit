#include <lit/vm/lit_vm.h>
#include <lit/vm/lit_object.h>
#include <lit/debug/lit_debug.h>
#include <lit/mem/lit_mem.h>
#include <lit/util/lit_fs.h>

#include <stdio.h>
#include <math.h>
#include <string.h>
#include <time.h>

static void push_root(LitVm* vm, LitObject* object) {
	assert(vm->root_count < LIT_ROOT_MAX);
	vm->roots[vm->root_count++] = object;
}

static LitObject* peek_root(LitVm* vm, uint8_t distance) {
	assert(vm->root_count - distance + 1 > 0);
	return vm->roots[vm->root_count - distance - 1];
}

static void pop_root(LitVm* vm) {
	assert(vm->root_count > 0);
	vm->root_count--;
}

static void reset_stack(LitVm* vm) {
	if (vm->fiber != NULL) {
		vm->fiber->stack_top = vm->fiber->stack;
	}
}

static void define_native(LitVm* vm, const char* name, LitNativeFn function) {
	LitState* state = vm->state;

	push_root(vm, (LitObject *) lit_create_native(state, function));
	push_root(vm, (LitObject *) lit_copy_string(state, name, (uint) strlen(name)));
	lit_table_set(state, &vm->globals, (LitString*) peek_root(vm, 0), OBJECT_VALUE(peek_root(vm, 1)));
	pop_root(vm);
	pop_root(vm);
}

static LitValue time_native(LitVm* vm, uint arg_count, LitValue* args) {
	return NUMBER_VALUE((double) clock() / CLOCKS_PER_SEC);
}

static LitValue print_native(LitVm* vm, uint arg_count, LitValue* args) {
	for (uint i = 0; i < arg_count; i++) {
		lit_print_value(args[i]);
		printf("\n");
	}

	return NULL_VALUE;
}

void lit_init_vm(LitState* state, LitVm* vm) {
	vm->state = state;
	vm->objects = NULL;
	vm->fiber = NULL;
	vm->root_count = 0;

	lit_init_table(&vm->strings);
	lit_init_table(&vm->globals);
	lit_init_table(&vm->modules);
}

void lit_define_std(LitVm* vm) {
	define_native(vm, "time", time_native);
	define_native(vm, "print", print_native);
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

	for (int i = vm->fiber->frame_count - 1; i >= 0; i--) {
		LitCallFrame* frame = &vm->fiber->frames[i];
		LitFunction* function = frame->function;

		fprintf(stderr, "[line %d] in %s()\n", lit_chunk_get_line(&function->chunk, frame->ip - function->chunk.code - 1), function->name->chars);
	}

	reset_stack(vm);
}

static inline bool is_falsey(LitValue value) {
	if (IS_NUMBER(value)) {
		return AS_NUMBER(value) == 0;
	}

	return IS_NULL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static bool call(LitVm* vm, LitFunction* function, uint8_t arg_count) {
	LitFiber* fiber = vm->fiber;

	if (fiber->frame_count == LIT_CALL_FRAMES_MAX) {
		runtime_error(vm, "Stack overflow");
		return false;
	}

	LitCallFrame* frame = &fiber->frames[fiber->frame_count++];

	frame->function = function;
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
				return call(vm, AS_FUNCTION(callee), arg_count);
			}

			case OBJECT_NATIVE: {
				LitNativeFn native = AS_NATIVE(callee);
				LitValue result = native(vm, arg_count, vm->fiber->stack_top - arg_count);
				vm->fiber->stack_top -= arg_count + 1;
				lit_push(vm, result);

				return true;
			}

			default: break;
		}
	}

	runtime_error(vm, "Can only call functions and classes.");
	return false;
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
	register LitCallFrame* frame = &fiber->frames[fiber->frame_count - 1];
	register LitChunk* current_chunk = &frame->function->chunk;
	register uint8_t* ip = frame->ip = current_chunk->code;
	register LitValue* slots = frame->slots;
	register LitValue* privates = fiber->module->privates;

	// Has to be inside of the function in order for goto to work
	static void* dispatch_table[] = {
#define OPCODE(name, effect) &&OP_##name,
#include <lit/vm/lit_opcodes.h>
#undef OPCODE
	};

#define READ_BYTE() (*ip++)
#define READ_SHORT() (ip += 2, (uint16_t) ((ip[-2] << 8) | ip[-1]))
#define CASE_CODE(name) OP_##name:
#define READ_CONSTANT() (current_chunk->constants.values[READ_BYTE()])
#define READ_CONSTANT_LONG() (current_chunk->constants.values[READ_SHORT()])
#define READ_STRING() AS_STRING(READ_CONSTANT())
#define PEEK(distance) fiber->stack_top[-1 - distance]
#define READ_FRAME() frame = &fiber->frames[fiber->frame_count - 1]; \
	current_chunk = &frame->function->chunk; \
	ip = frame->ip; \
	slots = frame->slots;

#define WRITE_FRAME() frame->ip = ip;
#define RETURN_ERROR() return (LitInterpretResult) {INTERPRET_RUNTIME_ERROR, NULL_VALUE};

#define BINARY_OP(type, op) \
    do { \
			if (!IS_NUMBER(PEEK(0)) || !IS_NUMBER(PEEK(1))) { \
				runtime_error(vm, "Operands must be numbers"); \
				RETURN_ERROR() \
			} \
      double b = AS_NUMBER(lit_pop(vm)); \
      double a = AS_NUMBER(lit_pop(vm)); \
      lit_push(vm, type(a op b)); \
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
			lit_pop(vm);
			continue;
		}

		CASE_CODE(POP_MULTIPLE) {
			uint8_t index = READ_BYTE();
			fiber->stack_top -= index;

			continue;
		}

		CASE_CODE(RETURN) {
			LitValue result = lit_pop(vm);

			WRITE_FRAME()
			fiber->frame_count--;

			if (fiber->frame_count == 0) {
				lit_pop(vm);
				return (LitInterpretResult) { INTERPRET_OK, result };
			}

			fiber->stack_top = frame->slots;
			lit_push(vm, result);
			READ_FRAME()

			#ifdef LIT_TRACE_EXECUTION
				printf("== %s ==\n", frame->function->name->chars);
			#endif

			continue;
		}

		CASE_CODE(CONSTANT) {
			lit_push(vm, READ_CONSTANT());
			continue;
		}

		CASE_CODE(CONSTANT_LONG) {
			lit_push(vm, READ_CONSTANT_LONG());
			continue;
		}

		CASE_CODE(TRUE) {
			lit_push(vm, TRUE_VALUE);
			continue;
		}

		CASE_CODE(FALSE) {
			lit_push(vm, FALSE_VALUE);
			continue;
		}

		CASE_CODE(NULL) {
			lit_push(vm, NULL_VALUE);
			continue;
		}

		CASE_CODE(NEGATE) {
			if (!IS_NUMBER(PEEK(0))) {
				runtime_error(vm, "Operand must be a number");
				RETURN_ERROR()
			}

			lit_push(vm, NUMBER_VALUE(-AS_NUMBER(lit_pop(vm))));
			continue;
		}

		CASE_CODE(NOT) {
			lit_push(vm, BOOL_VALUE(is_falsey(lit_pop(vm))));
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

      double b = AS_NUMBER(lit_pop(vm));
      double a = AS_NUMBER(lit_pop(vm));

      lit_push(vm, NUMBER_VALUE(fmod(a, b)));

      continue;
		}

		CASE_CODE(EQUAL) {
			LitValue a = lit_pop(vm);
			LitValue b = lit_pop(vm);

			lit_push(vm, BOOL_VALUE(a == b));
			continue;
		}

		CASE_CODE(NOT_EQUAL) {
			LitValue a = lit_pop(vm);
			LitValue b = lit_pop(vm);

			lit_push(vm, BOOL_VALUE(a != b));
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
			LitString* name = READ_STRING();
			lit_table_set(state, &vm->globals, name, PEEK(0));

			continue;
		}

		CASE_CODE(GET_GLOBAL) {
			LitString* name = READ_STRING();
			LitValue value;

			if (!lit_table_get(&vm->globals, name, &value)) {
				lit_push(vm, NULL_VALUE);
			} else {
				lit_push(vm, value);
			}

			continue;
		}

		CASE_CODE(SET_LOCAL) {
			uint8_t index = READ_BYTE();
			slots[index] = PEEK(0);

			continue;
		}

		CASE_CODE(GET_LOCAL) {
			lit_push(vm, slots[READ_BYTE()]);
			continue;
		}

		CASE_CODE(SET_LOCAL_LONG) {
			uint8_t index = READ_SHORT();
			slots[index] = PEEK(0);

			continue;
		}

		CASE_CODE(GET_LOCAL_LONG) {
			lit_push(vm, slots[READ_SHORT()]);
			continue;
		}

		CASE_CODE(SET_PRIVATE) {
			uint8_t index = READ_BYTE();
			privates[index] = PEEK(0);

			continue;
		}

		CASE_CODE(GET_PRIVATE) {
			lit_push(vm, privates[READ_BYTE()]);
			continue;
		}

		CASE_CODE(SET_PRIVATE_LONG) {
			uint8_t index = READ_SHORT();
			privates[index] = PEEK(0);

			continue;
		}

		CASE_CODE(GET_PRIVATE_LONG) {
			lit_push(vm, privates[READ_SHORT()]);
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
			LitValue name = lit_pop(vm);

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
				lit_push(vm, AS_MODULE(existing_module)->return_value);
				continue;
			}

			const char* source = lit_read_file(full_path);

			if (source == NULL) {
				runtime_error(vm, "Failed to open '%s'", full_path);
				RETURN_ERROR()
			}

			lit_push(vm, lit_internal_interpret(state, module_name, source).result);

			#ifdef LIT_TRACE_EXECUTION
				printf("== %s ==\n", frame->function->name->chars);
			#endif

			continue;
		}

		printf("Unknown op code!");
		break;
	}

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