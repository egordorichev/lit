#include <lit/vm/lit_chunk.h>
#include <lit/mem/lit_mem.h>

void lit_init_chunk(LitChunk* chunk) {
	chunk->count = 0;
	chunk->capacity = 0;
	chunk->code = NULL;

	lit_init_value_array(&chunk->constants);
}

void lit_free_chunk(LitState* state, LitChunk* chunk) {
	LIT_FREE_ARRAY(state, uint8_t, chunk->code, chunk->capacity);

	lit_free_value_array(state, &chunk->constants);
	lit_init_chunk(chunk);

}

void lit_write_chunk(LitState* state, LitChunk* chunk, uint8_t byte) {
	if (chunk->capacity < chunk->count + 1) {
		uint old_capacity = chunk->capacity;

		chunk->capacity = LIT_GROW_CAPACITY(old_capacity);
		chunk->code = LIT_GROW_ARRAY(state, chunk->code, uint8_t, old_capacity, chunk->capacity);
	}

	chunk->code[chunk->count] = byte;
	chunk->count++;
}

uint lit_chunk_add_constant(LitState* state, LitChunk* chunk, LitValue constant) {
	lit_write_value_array(state, &chunk->constants, constant);
	return chunk->constants.count - 1;
}

void lit_shrink_chunk(LitState* state, LitChunk* chunk) {
	if (chunk->capacity > chunk->count) {
		uint old_capacity = chunk->capacity;

		chunk->capacity = chunk->count;
		chunk->code = LIT_GROW_ARRAY(state, chunk->code, uint8_t, old_capacity, chunk->capacity);
	}
}
