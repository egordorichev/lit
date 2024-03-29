#include "lit/vm/lit_chunk.h"
#include "lit/mem/lit_mem.h"
#include "lit/state/lit_state.h"

void lit_init_chunk(LitChunk* chunk) {
	chunk->count = 0;
    chunk->capacity = 0;
    chunk->code = NULL;

    chunk->has_line_info = true;
	chunk->line_count = 0;
	chunk->line_capacity = 0;
	chunk->lines = NULL;

	lit_init_values(&chunk->constants);
}

void lit_free_chunk(LitState* state, LitChunk* chunk) {
	LIT_FREE_ARRAY(state, uint64_t, chunk->code, chunk->capacity);
	LIT_FREE_ARRAY(state, uint16_t , chunk->lines, chunk->line_capacity);

	lit_free_values(state, &chunk->constants);
	lit_init_chunk(chunk);
}

void lit_write_chunk(LitState* state, LitChunk* chunk, uint64_t word, uint16_t line) {
	if (chunk->capacity < chunk->count + 1) {
		uint old_capacity = chunk->capacity;

		chunk->capacity = LIT_GROW_CAPACITY(old_capacity);
		chunk->code = LIT_GROW_ARRAY(state, chunk->code, uint64_t, old_capacity, chunk->capacity);
	}

	chunk->code[chunk->count] = word;
	chunk->count++;

	if (!chunk->has_line_info) {
		return;
	}

	if (chunk->line_capacity < chunk->line_count + 4) {
		uint old_capacity = chunk->line_capacity;

		chunk->line_capacity = LIT_GROW_CAPACITY(chunk->line_capacity);
		chunk->lines = LIT_GROW_ARRAY(state, chunk->lines, uint16_t, old_capacity, chunk->line_capacity);

        if (old_capacity == 0) {
			chunk->lines[0] = 0;
			chunk->lines[1] = 0;
		}
	}

	uint line_index = chunk->line_count;
	uint value = chunk->lines[line_index];

	if (value != 0 && value != line) {
        chunk->line_count += 2;
        line_index = chunk->line_count;
		chunk->lines[line_index + 1] = 0;
	}

	chunk->lines[line_index] = line;
	chunk->lines[line_index + 1]++;
}

uint lit_chunk_add_constant(LitState* state, LitChunk* chunk, LitValue constant) {
	for (uint i = 0; i < chunk->constants.count; i++) {
		if (chunk->constants.values[i] == constant) {
			return i;
		}
	}

	lit_push_value_root(state, constant);
	lit_values_write(state, &chunk->constants, constant);
	lit_pop_root(state);

	return chunk->constants.count - 1;
}

uint lit_chunk_get_line(LitChunk* chunk, uint offset) {
	if (!chunk->has_line_info) {
		return 0;
	}

	uint rle = 0;
	uint line = 0;
	uint index = 0;

	for (uint i = 0; i <= offset; i++) {
		if (rle > 0) {
			rle--;
			continue;
		}

		line = chunk->lines[index];
		rle = chunk->lines[index + 1];

		if (rle > 0) {
			rle--;
		}

		index += 2;
	}

	return line;
}

void lit_shrink_chunk(LitState* state, LitChunk* chunk) {
	if (chunk->capacity > chunk->count) {
		uint old_capacity = chunk->capacity;

		chunk->capacity = chunk->count;
		chunk->code = LIT_GROW_ARRAY(state, chunk->code, uint64_t, old_capacity, chunk->capacity);
	}

	if (chunk->line_capacity > chunk->line_count) {
		uint old_capacity = chunk->line_capacity;

		chunk->line_capacity = chunk->line_count + 2;
		chunk->lines = LIT_GROW_ARRAY(state, chunk->lines, uint16_t, old_capacity, chunk->line_capacity);
	}
}