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
#define READ_IP() ip = vm->ip; current_chunk = vm->chunk;
#define WRITE_IP() vm->ip = ip; vm->chunk = current_chunk;
#define CASE_CODE(name) OP_##name:
#define READ_CONSTANT() (current_chunk->constants.values[READ_BYTE()])

#define BINARY_OP(op) \
    do { \
      double b = lit_pop(vm); \
      double a = lit_pop(vm); \
      lit_push(vm, a op b); \
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

		CASE_CODE(RETURN) {
			printf("=> ");
			lit_print_value(lit_pop(vm));
			printf("\n");

			return INTERPRET_OK;
		};

		CASE_CODE(CONSTANT) {
			lit_push(vm, READ_CONSTANT());
			continue;
		};

		CASE_CODE(NEGATE) {
			lit_push(vm, -lit_pop(vm));
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

#undef BINARY_OP
#undef READ_CONSTANT
#undef CASE_CODE
#undef WRITE_IP
#undef READ_IP
#undef READ_BYTE

	return INTERPRET_RUNTIME_ERROR;
}