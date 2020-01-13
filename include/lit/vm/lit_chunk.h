#ifndef LIT_CHUNK_H
#define LIT_CHUNK_H

#include <lit/lit_common.h>
#include <lit/state/lit_state.h>

typedef enum {
#define OPCODE(name) OP_##name,
#include <lit/vm/lit_opcodes.h>
#undef OPCODE
} LitOpCode;

typedef struct {
	uint count;
	uint capacity;

	uint8_t* code;
} LitChunk;

void lit_init_chunk(LitChunk* chunk);
void lit_free_chunk(LitState* state, LitChunk* chunk);
void lit_write_chunk(LitState* state, LitChunk* chunk, uint8_t byte);
void lit_shrink_chunk(LitState* state, LitChunk* chunk);

#endif