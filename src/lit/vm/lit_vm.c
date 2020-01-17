#include <lit/vm/lit_vm.h>
#include <lit/debug/lit_debug.h>
#include <lit/lit.h>

#include <stdio.h>

void reset_stack(LitVm* vm) {
	vm->stack_top = vm->stack;
}

void lit_init_vm(LitVm* vm) {
	reset_stack(vm);
}

void lit_free_vm(LitVm* vm) {
	lit_init_vm(vm);
}

void lit_push(LitVm* vm, LitValue value) {
	*vm->stack_top = value;
	vm->stack_top++;
}

LitValue lit_pop(LitVm* vm) {
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

	size_t instruction = vm->ip - vm->chunk->code;
	int line = vm->chunk->lines[instruction];
	fprintf(stderr, "[line %d] in script\n", line);

	reset_stack(vm);
}

LitInterpretResult lit_interpret_chunk(LitState* state, LitChunk* chunk) {
	register LitVm *vm = state->vm;

	vm->chunk = chunk;
	vm->ip = chunk->code;

	register uint8_t* ip = vm->ip;
	register LitChunk* current_chunk = chunk;

	// Has to be inside of the function in order for goto to work
	static void* dispatch_table[] = {
#define OPCODE(name) &&OP_##name,
#include <lit/vm/lit_opcodes.h>
#undef OPCODE
	};

#define READ_BYTE() (*ip++)
#define READ_SHORT() (ip += 2, (uint16_t) ((ip[-2] << 8) | ip[-1]))
#define READ_IP() ip = vm->ip; current_chunk = vm->chunk;
#define WRITE_IP() vm->ip = ip; vm->chunk = current_chunk;
#define CASE_CODE(name) OP_##name:
#define READ_CONSTANT() (current_chunk->constants.values[READ_BYTE()])
#define READ_CONSTANT_LONG() (current_chunk->constants.values[READ_SHORT()])
#define PEEK(distance) vm->stack_top[-1 - distance]

#define BINARY_OP(op) \
    do { \
			if (!IS_NUMBER(PEEK(0)) || !IS_NUMBER(PEEK(1))) { \
				runtime_error(vm, "Operands must be numbers"); \
				return INTERPRET_RUNTIME_ERROR; \
			} \
      double b = AS_NUMBER(lit_pop(vm)); \
      double a = AS_NUMBER(lit_pop(vm)); \
      lit_push(vm, NUMBER_VAL(a op b)); \
    } while (false);

#ifdef LIT_TRACE_EXECUTION
	uint8_t instruction;
#endif

	while (true) {
#ifdef LIT_TRACE_STACK
		trace_stack(vm);
#endif

#ifdef LIT_TRACE_EXECUTION
		instruction = *ip++;
		lit_disassemble_instruction(vm->chunk, (uint) (ip - current_chunk->code - 1));
		goto *dispatch_table[instruction];
#else
		goto *dispatch_table[*ip++];
#endif

		CASE_CODE(POP) {
			lit_pop(vm);
			continue;
		};

		CASE_CODE(RETURN) {
			return INTERPRET_OK;
		};

		CASE_CODE(CONSTANT) {
			lit_push(vm, READ_CONSTANT());
			continue;
		};

		CASE_CODE(CONSTANT_LONG) {
			lit_push(vm, READ_CONSTANT_LONG());
			continue;
		};

		CASE_CODE(TRUE) {
			lit_push(vm, TRUE_VAL);
			continue;
		};

		CASE_CODE(FALSE) {
			lit_push(vm, FALSE_VAL);
			continue;
		};

		CASE_CODE(NULL) {
			lit_push(vm, NULL_VAL);
			continue;
		};

		CASE_CODE(NEGATE) {
			if (!IS_NUMBER(PEEK(0))) {
				runtime_error(vm, "Operand must be a number");
				return INTERPRET_RUNTIME_ERROR;
			}

			lit_push(vm, NUMBER_VAL(-AS_NUMBER(lit_pop(vm))));
			continue;
		}

		CASE_CODE(ADD) {
			BINARY_OP(+)
			continue;
		}

		CASE_CODE(SUBTRACT) {
			BINARY_OP(-)
			continue;
		}

		CASE_CODE(MULTIPLY) {
			BINARY_OP(*)
			continue;
		}

		CASE_CODE(DIVIDE) {
			BINARY_OP(/)
			continue;
		}

		printf("Unknown op code!");
		break;
	}

#undef PEEK
#undef BINARY_OP
#undef READ_CONSTANT_LONG
#undef READ_CONSTANT
#undef CASE_CODE
#undef WRITE_IP
#undef READ_IP
#undef READ_SHORT
#undef READ_BYTE

	return INTERPRET_RUNTIME_ERROR;
}