#include "lit/debug/lit_debug.h"
#include "lit/vm/lit_object.h"

#include <stdio.h>

void lit_disassemble_module(LitModule* module, const char* source) {
	lit_disassemble_chunk(&module->main_function->chunk, module->main_function->name->chars, source);
}

static void print_constant(LitValue value) {
	if (IS_FUNCTION(value)) {
		LitString *function_name = AS_FUNCTION(value)->name;
		printf("%sfunction %.*s%s", COLOR_CYAN, function_name->length, function_name->chars, COLOR_RESET);
	} else if (IS_CLOSURE(value)) {
		LitString *function_name = AS_CLOSURE(value)->function->name;
		printf("%sclosure %.*s%s", COLOR_CYAN, function_name->length, function_name->chars, COLOR_RESET);
	} else if (IS_CLOSURE_PROTOTYPE(value)) {
		LitString *function_name = AS_CLOSURE_PROTOTYPE(value)->function->name;
		printf("%sclosure prototype %.*s%s", COLOR_CYAN, function_name->length, function_name->chars, COLOR_RESET);
	} else if (IS_STRING(value)) {
		LitString *string = AS_STRING(value);
		printf("%s\"%.*s\"%s", COLOR_CYAN, string->length, string->chars, COLOR_RESET);
	} else if (IS_NUMBER(value)) {
		printf("%s%g%s", COLOR_CYAN, AS_NUMBER(value), COLOR_RESET);
	} else {
		printf("unknown");
	}
}

void lit_disassemble_chunk(LitChunk* chunk, const char* name, const char* source) {
	LitValues* values = &chunk->constants;
	printf("^^ %s ^^\n", name);

	if (values->count > 0) {
		printf("%sconstants:%s\n", COLOR_MAGENTA, COLOR_RESET);

		for (uint i = 0; i < values->count; i++) {
			LitValue value = values->values[i];

			printf("% 4d ", i);
			print_constant(value);
			printf("\n");
		}
	}

	printf("%stext:%s\n", COLOR_MAGENTA, COLOR_RESET);

	for (uint offset = 0; offset < chunk->count; offset++) {
		lit_disassemble_instruction(chunk, offset, source, false);
	}


	printf("%shex:%s\n", COLOR_MAGENTA, COLOR_RESET);

	for (uint offset = 0; offset < chunk->count; offset++) {
		printf("%08lX ", chunk->code[offset]);
	}

	printf("\n");

	printf("vv %s vv\n", name);
}

typedef void (*LitDebugInstructionFn)(uint64_t instruction, const char* name);

static void print_abc_instruction(uint64_t instruction, const char* name) {
	printf("%s%s%s%*s %lu \t%lu \t%lu\n", COLOR_YELLOW, name, COLOR_RESET, LIT_LONGEST_OP_NAME - (int) strlen(name), "", LIT_INSTRUCTION_A(instruction), LIT_INSTRUCTION_B(instruction), LIT_INSTRUCTION_C(instruction));
}

static void print_abx_instruction(uint64_t instruction, const char* name) {
	printf("%s%s%s%*s %lu \t%lu\n", COLOR_YELLOW, name, COLOR_RESET, LIT_LONGEST_OP_NAME - (int) strlen(name), "", LIT_INSTRUCTION_A(instruction), LIT_INSTRUCTION_BX(instruction));
}

static void print_asbx_instruction(uint64_t instruction, const char* name) {
	printf("%s%s%s%*s %lu \t%li\n", COLOR_YELLOW, name, COLOR_RESET, LIT_LONGEST_OP_NAME - (int) strlen(name), "", LIT_INSTRUCTION_A(instruction), LIT_INSTRUCTION_SBX(instruction));
}

static void print_register(uint16_t reg) {
	printf(" \t%hu", reg);
}

static void print_constant_arg(LitChunk* chunk, uint16_t arg, bool indent) {
	arg &= 0xff;

	printf("%sc%hu (", indent ? " \t" : "", arg);
	print_constant(chunk->constants.values[arg]);
	printf(")");
}

static void print_constant_or_register(LitChunk* chunk, uint16_t arg) {
	if (IS_BIT_SET(arg, 8)) {
		print_constant_arg(chunk, arg, true);
	} else {
		print_register(arg);
	}
}

static void print_unary_instruction(LitChunk* chunk, uint64_t instruction, const char* name) {
	printf("%s%s%s%*s %lu", COLOR_YELLOW, name, COLOR_RESET, LIT_LONGEST_OP_NAME - (int) strlen(name), "", LIT_INSTRUCTION_A(instruction));
	print_constant_or_register(chunk, LIT_INSTRUCTION_B(instruction));
	printf("\n");
}

static void print_binary_instruction(LitChunk* chunk, uint64_t instruction, const char* name) {
	printf("%s%s%s%*s %lu", COLOR_YELLOW, name, COLOR_RESET, LIT_LONGEST_OP_NAME - (int) strlen(name), "", LIT_INSTRUCTION_A(instruction));

	print_constant_or_register(chunk, LIT_INSTRUCTION_B(instruction));
	print_constant_or_register(chunk, LIT_INSTRUCTION_C(instruction));

	printf("\n");
}

static void print_global_instruction(LitChunk* chunk, uint64_t instruction, const char* name) {
	printf("%s%s%s%*s", COLOR_YELLOW, name, COLOR_RESET, LIT_LONGEST_OP_NAME - (int) strlen(name), "");

	print_constant_arg(chunk, LIT_INSTRUCTION_BX(instruction), false);
	print_constant_or_register(chunk, LIT_INSTRUCTION_A(instruction));

	printf("\n");
}

static LitDebugInstructionFn debug_instruction_functions[] = {
	print_abc_instruction,
	print_abx_instruction,
	print_asbx_instruction
};

void lit_disassemble_instruction(LitChunk* chunk, uint offset, const char* source, bool force_line) {
	uint line = lit_chunk_get_line(chunk, offset);
	bool same = !chunk->has_line_info || (offset > 0 && line == lit_chunk_get_line(chunk, offset - 1));

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

				printf("%s        %.*s%s\n", COLOR_RED, next_line ? (int) (next_line - output_line) : (int) strlen(prev_line), output_line, COLOR_RESET);
				break;
			}
		}
	}

	printf("%04d ", offset);

	if (same && !force_line) {
		printf("   | ");
	} else {
		printf("%s%4d%s ", COLOR_BLUE, line, COLOR_RESET);
	}

	uint64_t instruction = chunk->code[offset];
	uint8_t opcode = LIT_INSTRUCTION_OPCODE(instruction);

	switch (opcode) {
		case OP_MOVE: print_binary_instruction(chunk, instruction, "MOVE"); break;
		case OP_ADD: print_binary_instruction(chunk, instruction, "ADD"); break;
		case OP_SUBTRACT: print_binary_instruction(chunk, instruction, "SUBTRACT"); break;
		case OP_MULTIPLY: print_binary_instruction(chunk, instruction, "MULTIPLY"); break;
		case OP_DIVIDE: print_binary_instruction(chunk, instruction, "DIVIDE"); break;
		case OP_NEGATE: print_unary_instruction(chunk, instruction, "NEGATE"); break;
		case OP_NOT: print_unary_instruction(chunk, instruction, "NOT"); break;

		case OP_EQUAL: print_binary_instruction(chunk, instruction, "EQUAL"); break;
		case OP_LESS: print_binary_instruction(chunk, instruction, "LESS"); break;
		case OP_LESS_EQUAL: print_binary_instruction(chunk, instruction, "LESS_EQUAL"); break;

		case OP_SET_GLOBAL: print_global_instruction(chunk, instruction, "SET_GLOBAL"); break;
		case OP_GET_GLOBAL: print_global_instruction(chunk, instruction, "GET_GLOBAL"); break;

		default: {
			switch (opcode) {
				// A simple way to automatically generate case printers for all the opcodes
				#define OPCODE(name, string_name, type) case OP_##name: { \
	        debug_instruction_functions[(int) type](instruction, string_name); \
	        break; \
	      }

				#include "lit/vm/lit_opcodes.h"
				#undef OPCODE

				default: {
					printf("Unknown opcode %d\n", opcode);
					break;
				}
			}
		}
	}
}

void lit_trace_frame(LitFiber* fiber) {
	#ifdef LIT_TRACE_STACK
	if (fiber == NULL) {
		return;
	}

	LitCallFrame* frame = &fiber->frames[fiber->frame_count - 1];
	printf("== fiber %p f%i %s (expects %i, max %i, added %i, current %i, exits %i) ==\n", fiber, fiber->frame_count - 1, frame->function->name->chars, frame->function->arg_count, frame->function->max_registers, frame->function->max_registers + (int) (fiber->stack_top - fiber->stack), fiber->stack_capacity, frame->return_address == NULL);
	#endif
}