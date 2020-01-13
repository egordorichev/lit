#include <lit/vm/lit_chunk.h>
#include <lit/mem/lit_mem.h>

void lit_init_chunk(LitChunk* chunk) {
	chunk->count = 0;
	chunk->capacity = 0;
	chunk->code = NULL;

	chunk->line_count = 0;
	chunk->line_capacity = 0;
	chunk->lines = NULL;

	lit_init_value_array(&chunk->constants);
}

void lit_free_chunk(LitState* state, LitChunk* chunk) {
	LIT_FREE_ARRAY(state, uint8_t, chunk->code, chunk->capacity);
	LIT_FREE_ARRAY(state, uint16_t , chunk->lines, chunk->line_capacity);

	lit_free_value_array(state, &chunk->constants);
	lit_init_chunk(chunk);
}

void lit_write_chunk(LitState* state, LitChunk* chunk, uint8_t byte, uint16_t line) {
	if (chunk->capacity < chunk->count + 1) {
		uint old_capacity = chunk->capacity;

		chunk->capacity = LIT_GROW_CAPACITY(old_capacity);
		chunk->code = LIT_GROW_ARRAY(state, chunk->code, uint8_t, old_capacity, chunk->capacity);
	}

	if (chunk->line_capacity < chunk->line_count + 4) {
		uint old_capacity = chunk->line_capacity;

		chunk->line_capacity = LIT_GROW_CAPACITY(chunk->line_capacity);
		chunk->lines = LIT_GROW_ARRAY(state, chunk->lines, uint16_t, old_capacity, chunk->line_capacity);
	}

	uint line_index = chunk->line_count * 2;
	uint value = chunk->lines[line_index];

	if (value != 0 && value != line) {
		chunk->line_count++;
		line_index = chunk->line_count * 2;
	}

	chunk->lines[line_index] = line;
	chunk->lines[line_index + 1]++;

	chunk->code[chunk->count] = byte;
	chunk->count++;
}

uint lit_chunk_add_constant(LitState* state, LitChunk* chunk, LitValue constant) {
	lit_write_value_array(state, &chunk->constants, constant);
	return chunk->constants.count - 1;
}

uint lit_chunk_get_line(LitChunk* chunk, uint offset) {
	uint rle = 0;
	uint line = 0;
	uint index = 0;

	for (uint i = 0; i < offset + 1; i++) {
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
		chunk->code = LIT_GROW_ARRAY(state, chunk->code, uint8_t, old_capacity, chunk->capacity);
	}

	if (chunk->line_capacity > chunk->line_count) {
		uint old_capacity = chunk->line_capacity;

		chunk->line_capacity = chunk->line_count;
		chunk->lines = LIT_GROW_ARRAY(state, chunk->lines, uint16_t, old_capacity, chunk->line_capacity);
	}
}
