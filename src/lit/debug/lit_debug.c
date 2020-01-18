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

static uint print_constant_op(const char* name, LitChunk* chunk, uint offset, bool big) {
	uint8_t constant = chunk->code[offset + 1];

	printf("%-16s %4d '", name, constant);
	lit_print_value(chunk->constants.values[constant]);
	printf("'\n");

	return offset + (big ? 3 : 2);
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
		case OP_POP: return print_simple_op("OP_POP", offset);
		case OP_RETURN: return print_simple_op("OP_RETURN", offset);
		case OP_CONSTANT: return print_constant_op("OP_CONSTANT", chunk, offset, false);
		case OP_CONSTANT_LONG: return print_constant_op("OP_CONSTANT_LONG", chunk, offset, true);
		case OP_TRUE: return print_simple_op("OP_TRUE", offset);
		case OP_FALSE: return print_simple_op("OP_FALSE", offset);
		case OP_NULL: return print_simple_op("OP_NULL", offset);
		case OP_NEGATE: return print_simple_op("OP_NEGATE", offset);
		case OP_NOT: return print_simple_op("OP_NOT", offset);
		case OP_ADD: return print_simple_op("OP_ADD", offset);
		case OP_SUBTRACT: return print_simple_op("OP_SUBTRACT", offset);
		case OP_MULTIPLY: return print_simple_op("OP_MULTIPLY", offset);
		case OP_DIVIDE: return print_simple_op("OP_DIVIDE", offset);
		case OP_EQUAL: return print_simple_op("OP_EQUAL", offset);
		case OP_NOT_EQUAL: return print_simple_op("OP_NOT_EQUAL", offset);
		case OP_GREATER: return print_simple_op("OP_GREATER", offset);
		case OP_GREATER_EQUAL: return print_simple_op("OP_GREATER_EQUAL", offset);
		case OP_LESS: return print_simple_op("OP_LESS", offset);
		case OP_LESS_EQUAL: return print_simple_op("OP_LESS_EQUAL", offset);
		case OP_PRINT: return print_simple_op("OP_PRINT", offset);

		default: {
			printf("Unknown opcode %d\n", instruction);
			return offset + 1;
		}
	}
}