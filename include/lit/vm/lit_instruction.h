#ifndef LIT_INSTRUCTION_H
#define LIT_INSTRUCTION_H

#include <math.h>

#define LIT_LONGEST_OP_NAME 13

typedef enum {
	#define OPCODE(name, a, b) OP_##name,
	#include "vm/lit_opcodes.h"
	#undef OPCODE
} LitOpCode;

typedef enum {
	LIT_INSTRUCTION_ABC,
	LIT_INSTRUCTION_ABX,
	LIT_INSTRUCTION_ASBX,
} LitInstructionType;

#define LIT_OPCODE_SIZE 0x3f
#define LIT_A_ARG_SIZE 0xff
#define LIT_B_ARG_SIZE 0x1ff
#define LIT_C_ARG_SIZE 0x1ff
#define LIT_BX_ARG_SIZE 0x3ffff // 18 bits max
#define LIT_SBX_ARG_SIZE 0x1ffff // 17 bits max

#define LIT_A_ARG_POSITION 6
#define LIT_B_ARG_POSITION 14
#define LIT_C_ARG_POSITION 23
#define LIT_BX_ARG_POSITION 14
#define LIT_SBX_ARG_POSITION 15
#define LIT_SBX_FLAG_POSITION 14

/*
 * Instruction can follow one of the three formats:
 *
 * ABC  opcode:6 bits (starting from bit 0), A:8 bits, B:9 bits, C:9 bits
 * ABx  opcode:6 bits (starting from bit 0), A:8 bits, Bx:18 bits
 * AsBx opcode:6 bits (starting from bit 0), A:8 bits, sBx:18 bits (signed)
 */

#define LIT_INSTRUCTION_OPCODE(instruction) (instruction & LIT_OPCODE_SIZE)
#define LIT_INSTRUCTION_A(instruction) ((instruction >> LIT_A_ARG_POSITION) & LIT_A_ARG_SIZE)
#define LIT_INSTRUCTION_B(instruction) ((instruction >> LIT_B_ARG_POSITION) & LIT_B_ARG_SIZE)
#define LIT_INSTRUCTION_C(instruction) ((instruction >> LIT_C_ARG_POSITION) & LIT_C_ARG_SIZE)
#define LIT_INSTRUCTION_BX(instruction) ((instruction >> LIT_BX_ARG_POSITION) & LIT_BX_ARG_SIZE)
#define LIT_INSTRUCTION_SBX(instruction) (((instruction >> LIT_SBX_ARG_POSITION) & LIT_SBX_ARG_SIZE) \
	* (((instruction >> LIT_SBX_FLAG_POSITION) & 0x1) == 1 ? -1 : 1))

#define LIT_READ_ABC_INSTRUCTION(instruction) uint8_t a = LIT_INSTRUCTION_A(instruction); \
	uint16_t b = LIT_INSTRUCTION_B(instruction); \
	uint16_t c = LIT_INSTRUCTION_C(instruction);

#define LIT_READ_BX_INSTRUCTION(instruction) uint8_t a = LIT_INSTRUCTION_A(instruction); \
	uint32_t bx = LIT_INSTRUCTION_BX(instruction);

#define LIT_READ_SBX_INSTRUCTION(instruction) uint8_t a = LIT_INSTRUCTION_A(instruction); \
	int32_t sbx = LIT_INSTRUCTION_SBX(instruction);

#define LIT_FORM_ABC_INSTRUCTION(opcode, a, b, c) (((opcode) & LIT_OPCODE_SIZE) \
	| (((a) & LIT_A_ARG_SIZE) << LIT_A_ARG_POSITION) \
	| (((b) & LIT_B_ARG_SIZE) << LIT_B_ARG_POSITION) \
	| (((c) & LIT_C_ARG_SIZE) << LIT_C_ARG_POSITION))

#define LIT_FORM_ABX_INSTRUCTION(opcode, a, bx) (((opcode) & LIT_OPCODE_SIZE) \
	| (((a) & LIT_A_ARG_SIZE) << LIT_A_ARG_POSITION) \
	| (((bx) & LIT_BX_ARG_SIZE) << LIT_BX_ARG_POSITION))

#define LIT_FORM_ASBX_INSTRUCTION(opcode, a, sbx) (((opcode) & LIT_OPCODE_SIZE) \
	| (((a) & LIT_A_ARG_SIZE) << LIT_A_ARG_POSITION) \
	| ((abs(sbx) & LIT_SBX_ARG_SIZE) << LIT_SBX_ARG_POSITION)) \
	| ((((sbx) < 0 ? 1 : 0) << LIT_SBX_FLAG_POSITION))

#endif