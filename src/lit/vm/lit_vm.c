#include <lit/vm/lit_vm.h>
#include <lit/vm/lit_object.h>
#include <lit/debug/lit_debug.h>
#include <lit/mem/lit_mem.h>

#include <stdio.h>
#include <math.h>

void reset_stack(LitVm* vm) {
	vm->stack_top = vm->stack;
}

void lit_init_vm(LitState* state, LitVm* vm) {
	reset_stack(vm);

	vm->state = state;
	vm->objects = NULL;
	vm->frame_count = 0;

	lit_init_table(&vm->strings);
	lit_init_table(&vm->globals);
}

void lit_free_vm(LitVm* vm) {
	lit_free_table(vm->state, &vm->strings);
	lit_free_table(vm->state, &vm->globals);
	lit_free_objects(vm->state, vm->objects);

	lit_init_vm(vm->state, vm);
}

void lit_push(LitVm* vm, LitValue value) {
	*vm->stack_top = value;
	vm->stack_top++;
}

LitValue lit_pop(LitVm* vm) {
	assert(vm->stack_top > vm->stack);
	vm->stack_top--;
	return *vm->stack_top;
}

static void trace_stack(LitVm* vm) {
	if (vm->stack_top == vm->stack) {
		return;
	}

	printf("        | ");

	for (LitValue* slot = vm->stack; slot < vm->stack_top; slot++) {
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

	for (int i = vm->frame_count - 1; i >= 0; i--) {
		LitCallFrame* frame = &vm->frames[i];
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
	if (vm->frame_count == LIT_CALL_FRAMES_MAX) {
		runtime_error(vm, "Stack overflow");
		return false;
	}

	LitCallFrame* frame = &vm->frames[vm->frame_count++];

	frame->function = function;
	frame->ip = function->chunk.code;

	frame->slots = vm->stack_top - arg_count - 1;

	uint function_arg_count = function->arg_count;

	if (arg_count < function_arg_count) {
		for (uint i = 0; i < function_arg_count - arg_count; i++) {
			lit_push(vm, NULL_VAL);
		}
	} else if (arg_count > function_arg_count) {
		for (uint i = 0; i < arg_count - function_arg_count; i++) {
			lit_pop(vm);
		}
	}

	return true;
}

static bool call_value(LitVm* vm, LitValue callee, uint8_t arg_count) {
	if (IS_OBJECT(callee)) {
		switch (OBJECT_TYPE(callee)) {
			case OBJECT_FUNCTION: {
				return call(vm, AS_FUNCTION(callee), arg_count);
			}

			default: break;
		}
	}

	runtime_error(vm, "Can only call functions and classes.");
	return false;
}

LitInterpretResult lit_interpret_function(LitState* state, LitFunction* function) {
	register LitVm *vm = state->vm;

	lit_push(vm, OBJECT_VAL(function));

	LitCallFrame* frame = &vm->frames[vm->frame_count++];

	frame->function = function;
	frame->ip = function->chunk.code;
	frame->slots = vm->stack;

	return lit_interpret_frame(state);
}

LitInterpretResult lit_interpret_frame(LitState* state) {
	register LitVm *vm = state->vm;
	register LitCallFrame* frame = &vm->frames[vm->frame_count - 1];
	register LitChunk* current_chunk = &frame->function->chunk;
	register uint8_t* ip = frame->ip = current_chunk->code;
	register LitValue* slots = frame->slots;

	// Has to be inside of the function in order for goto to work
	static void* dispatch_table[] = {
#define OPCODE(name) &&OP_##name,
#include <lit/vm/lit_opcodes.h>
#undef OPCODE
	};

#define READ_BYTE() (*ip++)
#define READ_SHORT() (ip += 2, (uint16_t) ((ip[-2] << 8) | ip[-1]))
#define CASE_CODE(name) OP_##name:
#define READ_CONSTANT() (current_chunk->constants.values[READ_BYTE()])
#define READ_CONSTANT_LONG() (current_chunk->constants.values[READ_SHORT()])
#define READ_STRING() AS_STRING(READ_CONSTANT())
#define PEEK(distance) vm->stack_top[-1 - distance]
#define READ_FRAME() frame = &vm->frames[vm->frame_count - 1]; \
	current_chunk = &frame->function->chunk; \
	ip = frame->ip; \
	slots = frame->slots;

#define WRITE_FRAME() frame->ip = ip;

#define BINARY_OP(type, op) \
    do { \
			if (!IS_NUMBER(PEEK(0)) || !IS_NUMBER(PEEK(1))) { \
				runtime_error(vm, "Operands must be numbers"); \
				return INTERPRET_RUNTIME_ERROR; \
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
			vm->stack_top -= index;

			continue;
		}

		CASE_CODE(RETURN) {
			LitValue result = lit_pop(vm);

			WRITE_FRAME()
			vm->frame_count--;

			if (vm->frame_count == 0) {
				lit_pop(vm);
				return INTERPRET_OK;
			}

			vm->stack_top = frame->slots;
			lit_push(vm, result);
			READ_FRAME()

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
			lit_push(vm, TRUE_VAL);
			continue;
		}

		CASE_CODE(FALSE) {
			lit_push(vm, FALSE_VAL);
			continue;
		}

		CASE_CODE(NULL) {
			lit_push(vm, NULL_VAL);
			continue;
		}

		CASE_CODE(NEGATE) {
			if (!IS_NUMBER(PEEK(0))) {
				runtime_error(vm, "Operand must be a number");
				return INTERPRET_RUNTIME_ERROR;
			}

			lit_push(vm, NUMBER_VAL(-AS_NUMBER(lit_pop(vm))));
			continue;
		}

		CASE_CODE(NOT) {
			lit_push(vm, BOOL_VAL(is_falsey(lit_pop(vm))));
			continue;
		}

		CASE_CODE(ADD) {
			BINARY_OP(NUMBER_VAL, +)
			continue;
		}

		CASE_CODE(SUBTRACT) {
			BINARY_OP(NUMBER_VAL, -)
			continue;
		}

		CASE_CODE(MULTIPLY) {
			BINARY_OP(NUMBER_VAL, *)
			continue;
		}

		CASE_CODE(DIVIDE) {
			BINARY_OP(NUMBER_VAL, /)
			continue;
		}

		CASE_CODE(MOD) {
			if (!IS_NUMBER(PEEK(0)) || !IS_NUMBER(PEEK(1))) {
				runtime_error(vm, "Operands must be numbers");
				return INTERPRET_RUNTIME_ERROR;
			}

      double b = AS_NUMBER(lit_pop(vm));
      double a = AS_NUMBER(lit_pop(vm));

      lit_push(vm, NUMBER_VAL(fmod(a, b)));

      continue;
		}

		CASE_CODE(EQUAL) {
			LitValue a = lit_pop(vm);
			LitValue b = lit_pop(vm);

			lit_push(vm, BOOL_VAL(a == b));
			continue;
		}

		CASE_CODE(NOT_EQUAL) {
			LitValue a = lit_pop(vm);
			LitValue b = lit_pop(vm);

			lit_push(vm, BOOL_VAL(a != b));
			continue;
		}

		CASE_CODE(GREATER) {
			BINARY_OP(BOOL_VAL, >)
			continue;
		}

		CASE_CODE(GREATER_EQUAL) {
			BINARY_OP(BOOL_VAL, >=)
			continue;
		}

		CASE_CODE(LESS) {
			BINARY_OP(BOOL_VAL, <)
			continue;
		}

		CASE_CODE(LESS_EQUAL) {
			BINARY_OP(BOOL_VAL, <=)
			continue;
		}

		CASE_CODE(PRINT) {
			lit_print_value(lit_pop(vm));
			printf("\n");
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
				lit_push(vm, NULL_VAL);
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
				return INTERPRET_RUNTIME_ERROR;
			}

			READ_FRAME()

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

	return INTERPRET_RUNTIME_ERROR;
}