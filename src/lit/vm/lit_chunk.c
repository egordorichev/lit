#include <lit/vm/lit_chunk.h>
#include <lit/mem/lit_mem.h>
#include <lit/state/lit_state.h>
#include <lit/util/lit_fs.h>

void lit_init_chunk(LitChunk* chunk) {
	chunk->count = 0;
	chunk->capacity = 0;
	chunk->code = NULL;

	chunk->line_count = 0;
	chunk->line_capacity = 0;
	chunk->lines = NULL;

	lit_init_values(&chunk->constants);
}

void lit_free_chunk(LitState* state, LitChunk* chunk) {
	LIT_FREE_ARRAY(state, uint8_t, chunk->code, chunk->capacity);
	LIT_FREE_ARRAY(state, uint16_t , chunk->lines, chunk->line_capacity);

	lit_free_values(state, &chunk->constants);
	lit_init_chunk(chunk);
}

void lit_write_chunk(LitState* state, LitChunk* chunk, uint8_t byte, uint16_t line) {
	if (chunk->capacity < chunk->count + 1) {
		uint old_capacity = chunk->capacity;

		chunk->capacity = LIT_GROW_CAPACITY(old_capacity);
		chunk->code = LIT_GROW_ARRAY(state, chunk->code, uint8_t, old_capacity, chunk->capacity);
	}

	chunk->code[chunk->count] = byte;
	chunk->count++;

	if (chunk->line_capacity < chunk->line_count + 2) {
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
	/*printf("\n");

	for (uint i = 0; i < chunk->line_count; i++) {
		printf("%d %d\n", chunk->lines[i * 2], chunk->lines[i * 2 + 1]);
	}*/

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

		chunk->line_capacity = chunk->line_count + 2;
		chunk->lines = LIT_GROW_ARRAY(state, chunk->lines, uint16_t, old_capacity, chunk->line_capacity);
	}
}

void lit_save_chunk(FILE* file, LitChunk* chunk) {
	lit_write_uint32_t(file, chunk->count);

	for (uint i = 0; i < chunk->count; i++) {
		lit_write_uint8_t(file, chunk->code[i]);
	}

	uint c = chunk->line_count * 2 + 2;
	lit_write_uint32_t(file, c);

	for (uint i = 0; i < c; i++) {
		lit_write_uint16_t(file, chunk->lines[i]);
	}

	lit_write_uint32_t(file, chunk->constants.count);

	for (uint i = 0; i < chunk->constants.count; i++) {
		lit_write_double(file, chunk->constants.values[i]);
	}
}

LitChunk* lit_load_chunk(FILE* file, LitState* state) {
	LitChunk* chunk = (LitChunk*) lit_reallocate(state, NULL, 0, sizeof(LitChunk));
	lit_init_chunk(chunk);
	
	uint count = lit_read_uint32_t(file);
	chunk->code = (uint8_t*) lit_reallocate(state, NULL, 0, sizeof(uint8_t) * count);
	chunk->count = count;
	chunk->capacity = count;
	
	for (uint i = 0; i < count; i++) {
		chunk->code[i] = lit_read_uint8_t(file);
	}

	count = lit_read_uint32_t(file);
	chunk->lines = (uint16_t*) lit_reallocate(state, NULL, 0, sizeof(uint16_t) * count);
	chunk->line_count = count;
	chunk->line_capacity = count;

	for (uint i = 0; i < count; i++) {
		chunk->lines[i] = lit_read_uint16_t(file);
	}

	count = lit_read_uint32_t(file);
	chunk->constants.values = (LitValue*) lit_reallocate(state, NULL, 0, sizeof(LitValue) * count);
	chunk->constants.count = count;
	chunk->constants.capacity = count;

	for (uint i = 0; i < count; i++) {
		chunk->constants.values[i] = (LitValue) lit_read_double(file);
	}
	
	return chunk;
}

void lit_emit_byte(LitState* state, LitChunk* chunk, uint8_t byte) {
	lit_write_chunk(state, chunk, byte, 1);
}

void lit_emit_bytes(LitState* state, LitChunk* chunk, uint8_t a, uint8_t b) {
	lit_write_chunk(state, chunk, a, 1);
	lit_write_chunk(state, chunk, b, 1);
}

void lit_emit_short(LitState* state, LitChunk* chunk, uint16_t value) {
	lit_emit_bytes(state, chunk, (uint8_t) ((value >> 8) & 0xff), (uint8_t) (value & 0xff));
}