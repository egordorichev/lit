#include <lit/debug/lit_debug.h>
#include <lit/vm/lit_object.h>
#include <stdio.h>

void lit_disassemble_module(LitModule* module, const char* source) {
	lit_disassemble_chunk(&module->main_function->chunk, module->main_function->name->chars, source);
}

void lit_disassemble_chunk(LitChunk* chunk, const char* name, const char* source) {
	LitValues* values = &chunk->constants;

	for (uint i = 0; i < values->count; i++) {
		LitValue value = values->values[i];

		if (IS_FUNCTION(value)) {
			LitFunction* function = AS_FUNCTION(value);
			lit_disassemble_chunk(&function->chunk, function->name->chars, source);
		}
	}

	printf("== %s ==\n", name);

	for (uint offset = 0; offset < chunk->count;) {
		offset = lit_disassemble_instruction(chunk, offset, source);
	}
}

static uint print_simple_op(const char* name, uint offset) {
	printf("%s%s%s\n", COLOR_YELLOW, name, COLOR_RESET);
	return offset + 1;
}

static uint print_constant_op(const char* name, LitChunk* chunk, uint offset, bool big) {
	uint8_t constant;

	if (big) {
		constant = (uint16_t) (chunk->code[offset + 1] << 8);
		constant |= chunk->code[offset + 2];
	} else {
		constant = chunk->code[offset + 1];
	}

	printf("%s%-16s%s %4d '", COLOR_YELLOW, name, COLOR_RESET, constant);
	lit_print_value(chunk->constants.values[constant]);
	printf("'\n");

	return offset + (big ? 3 : 2);
}

static uint print_byte_op(const char* name, LitChunk* chunk, uint offset) {
	uint8_t slot = chunk->code[offset + 1];
	printf("%s%-16s%s %4d\n", COLOR_YELLOW, name, COLOR_RESET, slot);

	return offset + 2;
}

static uint print_short_op(const char* name, LitChunk* chunk, uint offset) {
	uint16_t slot = (uint16_t) (chunk->code[offset + 1] << 8);
	slot |= chunk->code[offset + 2];

	printf("%s%-16s%s %4d\n", COLOR_YELLOW, name, COLOR_RESET, slot);

	return offset + 2;
}

static uint print_jump_op(const char* name, int sign, LitChunk* chunk, uint offset) {
	uint16_t jump = (uint16_t)(chunk->code[offset + 1] << 8);
	jump |= chunk->code[offset + 2];
	printf("%s%-16s%s %4d -> %d\n", COLOR_YELLOW, name, COLOR_RESET, offset, offset + 3 + sign * jump);
	return offset + 3;
}

static uint print_invoke_op(const char* name, LitChunk* chunk, uint offset) {
	uint8_t arg_count = chunk->code[offset + 1];

	uint8_t constant = chunk->code[offset + 2];
	constant |= chunk->code[offset + 3];

	printf("%s%-16s%s (%d args) %4d '", COLOR_YELLOW, name, COLOR_RESET, arg_count, constant);
	lit_print_value(chunk->constants.values[constant]);
	printf("'\n");

	return offset + 4;
}

uint lit_disassemble_instruction(LitChunk* chunk, uint offset, const char* source) {
	uint line = lit_chunk_get_line(chunk, offset);
	bool same = offset > 0 && line == lit_chunk_get_line(chunk, offset - 1);

	if (!same && source != NULL) {
		uint index = 0;
		char* current_line = (char*) source;

		while (current_line) {
			char* next_line = strchr(current_line, '\n');
			char* prev_line = current_line;

			index++;
			current_line = next_line ? (next_line + 1) : NULL;

			if (index == line) {
				char* output_line = prev_line ? prev_line : next_line;
				char c;

				while ((c = *output_line) && (c == '\t' || c == ' ')) {
					output_line++;
				}

				printf("%s        %.*s%s\n", COLOR_RED, (int) (next_line ? (next_line - output_line) : strlen(prev_line)), output_line, COLOR_RESET);
				break;
			}
		}
	}

	printf("%04d ", offset);

	if (same) {
		printf("   | ");
	} else {
		printf("%s%4d%s ", COLOR_BLUE, line, COLOR_RESET);
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
		case OP_POWER: return print_simple_op("OP_POWER", offset);
		case OP_DIVIDE: return print_simple_op("OP_DIVIDE", offset);
		case OP_FLOOR_DIVIDE: return print_simple_op("OP_FLOOR_DIVIDE", offset);
		case OP_MOD: return print_simple_op("OP_MOD", offset);
		case OP_BAND: return print_simple_op("OP_BAND", offset);
		case OP_BOR: return print_simple_op("OP_BOR", offset);
		case OP_BXOR: return print_simple_op("OP_BXOR", offset);
		case OP_LSHIFT: return print_simple_op("OP_LSHIFT", offset);
		case OP_RSHIFT: return print_simple_op("OP_RSHIFT", offset);
		case OP_BNOT: return print_simple_op("OP_BNOT", offset);
		case OP_EQUAL: return print_simple_op("OP_EQUAL", offset);
		case OP_GREATER: return print_simple_op("OP_GREATER", offset);
		case OP_GREATER_EQUAL: return print_simple_op("OP_GREATER_EQUAL", offset);
		case OP_LESS: return print_simple_op("OP_LESS", offset);
		case OP_LESS_EQUAL: return print_simple_op("OP_LESS_EQUAL", offset);

		case OP_SET_GLOBAL: return print_constant_op("OP_SET_GLOBAL", chunk, offset, true);
		case OP_GET_GLOBAL: return print_constant_op("OP_GET_GLOBAL", chunk, offset, true);

		case OP_SET_LOCAL: return print_byte_op("OP_SET_LOCAL", chunk, offset);
		case OP_GET_LOCAL: return print_byte_op("OP_GET_LOCAL", chunk, offset);
		case OP_SET_LOCAL_LONG: return print_short_op("OP_SET_LOCAL_LONG", chunk, offset);
		case OP_GET_LOCAL_LONG: return print_short_op("OP_GET_LOCAL_LONG", chunk, offset);

		case OP_SET_PRIVATE: return print_byte_op("OP_SET_PRIVATE", chunk, offset);
		case OP_GET_PRIVATE: return print_byte_op("OP_GET_PRIVATE", chunk, offset);
		case OP_SET_PRIVATE_LONG: return print_short_op("OP_SET_PRIVATE_LONG", chunk, offset);
		case OP_GET_PRIVATE_LONG: return print_short_op("OP_GET_PRIVATE_LONG", chunk, offset);

		case OP_SET_UPVALUE: return print_byte_op("OP_SET_UPVALUE", chunk, offset);
		case OP_GET_UPVALUE: return print_byte_op("OP_GET_UPVALUE", chunk, offset);

		case OP_JUMP_IF_FALSE: return print_jump_op("OP_JUMP_IF_FALSE", 1, chunk, offset);
		case OP_JUMP_IF_NULL: return print_jump_op("OP_JUMP_IF_NULL", 1, chunk, offset);
		case OP_JUMP: return print_jump_op("OP_JUMP", 1, chunk, offset);
		case OP_JUMP_BACK: return print_jump_op("OP_JUMP_BACK", -1, chunk, offset);
		case OP_AND: return print_jump_op("OP_AND", 1, chunk, offset);
		case OP_OR: return print_jump_op("OP_OR", 1, chunk, offset);
		case OP_NULL_OR: return print_jump_op("OP_NULL_OR", 1, chunk, offset);

		case OP_CALL: return print_byte_op("OP_CALL", chunk, offset);

		case OP_CLOSURE: {
			offset++;
			int16_t constant = (uint16_t) (chunk->code[offset] << 8);
			offset++;
			constant |= chunk->code[offset];

			printf("%-16s %4d ", "OP_CLOSURE", constant);
			lit_print_value(chunk->constants.values[constant]);
			printf("\n");

			LitFunction* function = AS_FUNCTION(chunk->constants.values[constant]);

			for (uint j = 0; j < function->upvalue_count; j++) {
				int is_local = chunk->code[offset++];
				int index = chunk->code[offset++];

				printf("%04d      |                     %s %d\n", offset - 2, is_local ? "local" : "upvalue", index);
			}

			return offset;
		}

		case OP_CLOSE_UPVALUE: return print_simple_op("OP_CLOSE_UPVALUE", offset);
		case OP_CLASS: return print_simple_op("OP_CLASS", offset);

		case OP_GET_FIELD: return print_simple_op("OP_GET_FIELD", offset);
		case OP_SET_FIELD: return print_simple_op("OP_SET_FIELD", offset);

		case OP_SUBSCRIPT_GET: return print_simple_op("OP_SUBSCRIPT_GET", offset);
		case OP_SUBSCRIPT_SET: return print_simple_op("OP_SUBSCRIPT_SET", offset);
		case OP_ARRAY: return print_simple_op("OP_ARRAY", offset);
		case OP_PUSH_ARRAY_ELEMENT: return print_simple_op("OP_PUSH_ARRAY_ELEMENT", offset);
		case OP_MAP: return print_simple_op("OP_MAP", offset);
		case OP_PUSH_MAP_ELEMENT: return print_simple_op("OP_PUSH_MAP_ELEMENT", offset);
		case OP_RANGE: return print_simple_op("OP_RANGE", offset);

		case OP_METHOD: return print_constant_op("OP_METHOD", chunk, offset, true);
		case OP_STATIC_FIELD: return print_constant_op("OP_STATIC_FIELD", chunk, offset, true);
		case OP_DEFINE_FIELD: return print_constant_op("OP_DEFINE_FIELD", chunk, offset, true);

		case OP_INVOKE: return print_invoke_op("OP_INVOKE", chunk, offset);
		case OP_INVOKE_SUPER: return print_invoke_op("OP_INVOKE_SUPER", chunk, offset);
		case OP_INVOKE_IGNORING: return print_invoke_op("OP_INVOKE_IGNORING", chunk, offset);
		case OP_INVOKE_SUPER_IGNORING: return print_invoke_op("OP_INVOKE_SUPER_IGNORING", chunk, offset);
		case OP_INHERIT: return print_simple_op("OP_INHERIT", offset);
		case OP_IS: return print_simple_op("OP_IS", offset);
		case OP_GET_SUPER_METHOD: return print_constant_op("OP_GET_SUPER_METHOD", chunk, offset, true);

		default: {
			printf("Unknown opcode %d\n", instruction);
			return offset + 1;
		}
	}
}