#include"debug/lit_debug.h"
#include "vm/lit_object.h"
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

	printf("^^ %s ^^\n", name);

	if (values->count > 0) {
		printf("%sconstants:%s\n", COLOR_MAGENTA, COLOR_RESET);

		for (uint i = 0; i < values->count; i++) {
			LitValue value = values->values[i];
			printf("% 4d ", i);

			if (IS_FUNCTION(value)) {
				LitString *function_name = AS_FUNCTION(value)->name;
				printf("%sfunction %.*s\n%s", COLOR_CYAN, function_name->length, function_name->chars, COLOR_RESET);
			} else if (IS_STRING(value)) {
				LitString *string = AS_STRING(value);
				printf("%s\"%.*s\"%s\n", COLOR_CYAN, string->length, string->chars, COLOR_RESET);
			} else if (IS_NUMBER(value)) {
				printf("%s%g%s\n", COLOR_CYAN, AS_NUMBER(value), COLOR_RESET);
			} else {
				printf("unknown\n");
			}
		}

	}

	printf("%stext:%s\n", COLOR_MAGENTA, COLOR_RESET);

	for (uint offset = 0; offset < chunk->count; offset++) {
		lit_disassemble_instruction(chunk, offset, source);
	}

	printf("vv %s vv\n", name);
}

typedef void (*LitDebugInstructionFn)(uint64_t instruction, const char* name);

static void print_abc_instruction(uint64_t instruction, const char* name) {
	printf("%s%s%s \t%lu \t%lu \t%lu\n", COLOR_YELLOW, name, COLOR_RESET, LIT_INSTRUCTION_A(instruction), LIT_INSTRUCTION_B(instruction), LIT_INSTRUCTION_C(instruction));
}

static void print_abx_instruction(uint64_t instruction, const char* name) {
	printf("%s%s%s \t%lu \t%lu\n", COLOR_YELLOW, name, COLOR_RESET, LIT_INSTRUCTION_A(instruction), LIT_INSTRUCTION_BX(instruction));
}

static void print_asbx_instruction(uint64_t instruction, const char* name) {
	printf("%s%s%s \t%lu \t%li\n", COLOR_YELLOW, name, COLOR_RESET, LIT_INSTRUCTION_A(instruction), LIT_INSTRUCTION_SBX(instruction));
}

static LitDebugInstructionFn debug_instruction_functions[] = {
	print_abc_instruction,
	print_abx_instruction,
	print_asbx_instruction
};

void lit_disassemble_instruction(LitChunk* chunk, uint offset, const char* source) {
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

	if (same) {
		printf("   | ");
	} else {
		printf("%s%4d%s ", COLOR_BLUE, line, COLOR_RESET);
	}

	uint64_t instruction = chunk->code[offset];
	uint8_t opcode = LIT_INSTRUCTION_OPCODE(instruction);

	switch (opcode) {
		// A simple way to automatically generate case printers for all the opcodes
		#define OPCODE(name, string_name, type) case OP_##name: { \
				debug_instruction_functions[(int) type](instruction, string_name); \
				break; \
			}

		#include "vm/lit_opcodes.h"
		#undef OPCODE

		default: {
			printf("Unknown opcode %d\n", opcode);
			break;
		}
	}
}

void lit_trace_frame(LitFiber* fiber) {
	#ifdef LIT_TRACE_STACK
	if (fiber == NULL) {
		return;
	}

	LitCallFrame* frame = &fiber->frames[fiber->frame_count - 1];
	printf("== fiber %p f%i %s (expects %i, max %i, added %i, current %i, exits %i) ==\n", fiber, fiber->frame_count - 1, frame->function->name->chars, frame->function->arg_count, frame->function->max_slots, frame->function->max_slots + (int) (fiber->stack_top - fiber->stack), fiber->stack_capacity, frame->return_to_c);
	#endif
}