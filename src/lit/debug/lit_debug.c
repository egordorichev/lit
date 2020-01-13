#include <lit/debug/lit_debug.h>
#include <stdio.h>

void lit_disassemble_chunk(LitChunk* chunk, const char* name) {
	printf("== %s ==\n", name);

	for (int offset = 0; offset < chunk->count;) {
		offset = lit_disassemble_instruction(chunk, offset);
	}
}

static int print_simple_op(const char* name, int offset) {
	printf("%s\n", name);
	return offset + 1;
}

uint lit_disassemble_instruction(LitChunk* chunk, uint offset) {
	printf("%04d ", offset);
	uint8_t instruction = chunk->code[offset];

	switch (instruction) {
		case OP_RETURN: return print_simple_op("OP_RETURN", offset);

		default: {
			printf("Unknown opcode %d\n", instruction);
			return offset + 1;
		}
	}
}