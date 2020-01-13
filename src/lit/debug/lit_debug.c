#include <lit/debug/lit_debug.h>
#include <stdio.h>

void lit_disassemble_chunk(LitChunk* chunk, const char* name) {
	printf("== %s ==\n", name);

	for (int offset = 0; offset < chunk->count;) {
		offset = lit_disassemble_instruction(chunk, offset);
	}
}

static uint print_simple_op(const char* name, uint offset) {
	printf("%s\n", name);
	return offset + 1;
}

static uint print_constant_op(const char* name, LitChunk* chunk, uint offset) {
	uint8_t constant = chunk->code[offset + 1];

	printf("%-16s %4d '", name, constant);
	lit_print_value(chunk->constants.values[constant]);
	printf("'\n");

	return offset + 2;
}

uint lit_disassemble_instruction(LitChunk* chunk, uint offset) {
	printf("%04d ", offset);
	uint line = lit_chunk_get_line(chunk, offset);

	if (offset > 0 && line == lit_chunk_get_line(chunk, offset - 1)) {
		printf("   | ");
	} else {
		printf("%4d ", line);
	}

	uint8_t instruction = chunk->code[offset];

	switch (instruction) {
		case OP_RETURN: return print_simple_op("OP_RETURN", offset);
		case OP_CONSTANT: return print_constant_op("OP_CONSTANT", chunk, offset);

		default: {
			printf("Unknown opcode %d\n", instruction);
			return offset + 1;
		}
	}
}