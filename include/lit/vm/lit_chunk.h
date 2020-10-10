#ifndef LIT_CHUNK_H
#define LIT_CHUNK_H

#include "lit_common.h"
#include "lit_predefines.h"
#include "vm/lit_value.h"

#include <stdio.h>

typedef enum {
#define OPCODE(name, effect) OP_##name,
#include "vm/lit_opcodes.h"
#undef OPCODE
} LitOpCode;

typedef struct {
	uint count;
	uint capacity;
	uint8_t* code;

	bool has_line_info;

	uint line_count;
	uint line_capacity;
	uint16_t* lines;

	LitValues constants;
} LitChunk;

void lit_init_chunk(LitChunk* chunk);
void lit_free_chunk(LitState* state, LitChunk* chunk);
void lit_write_chunk(LitState* state, LitChunk* chunk, uint8_t byte, uint16_t line);
uint lit_chunk_add_constant(LitState* state, LitChunk* chunk, LitValue constant);
uint lit_chunk_get_line(LitChunk* chunk, uint offset);
void lit_shrink_chunk(LitState* state, LitChunk* chunk);

void lit_emit_byte(LitState* state, LitChunk* chunk, uint8_t byte);
void lit_emit_bytes(LitState* state, LitChunk* chunk, uint8_t a, uint8_t b);
void lit_emit_short(LitState* state, LitChunk* chunk, uint16_t value);

#endif