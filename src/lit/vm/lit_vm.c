#include <lit/vm/lit_vm.h>
#include <lit/debug/lit_debug.h>
#include <lit/lit.h>

#include <stdio.h>

void lit_init_vm(LitVm* vm) {

}

void lit_free_vm(LitVm* vm) {

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

#ifdef LIT_TRACE_EXECUTION
	uint8_t instruction;
#endif

	while (true) {
#ifdef LIT_TRACE_EXECUTION
		instruction = *ip++;
		lit_disassemble_instruction(vm->chunk, (uint) (ip - current_chunk->code));
		goto *dispatch_table[instruction];
#else
		goto *dispatch_table[*ip++];
#endif

		CASE_CODE(CONSTANT) {
			lit_print_value(READ_CONSTANT());
			printf("\n");
			continue;
		};

		CASE_CODE(RETURN) {
			return INTERPRET_OK;
		};

		printf("Unknown op code!");
		break;
	}

#undef READ_CONSTANT
#undef CASE_CODE
#undef WRITE_IP
#undef READ_IP
#undef READ_BYTE

	return INTERPRET_RUNTIME_ERROR;
}