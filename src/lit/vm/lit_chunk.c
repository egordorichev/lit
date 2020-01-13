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

static void write_uint8_t(FILE* file, uint8_t byte) {
	fwrite(&byte, sizeof(uint8_t), 1, file);
}

static void write_uint16_t(FILE* file, uint16_t byte) {
	fwrite(&byte, sizeof(uint16_t), 1, file);
}

static void write_uint32_t(FILE* file, uint32_t byte) {
	fwrite(&byte, sizeof(uint32_t), 1, file);
}

static void write_double(FILE* file, double byte) {
	fwrite(&byte, sizeof(double), 1, file);
}

static uint8_t btmp;
static uint16_t stmp;
static uint32_t itmp;
static double dtmp;

static uint8_t read_uint8_t(FILE* file) {
	fread(&btmp, sizeof(uint8_t), 1, file);
	return btmp;
}

static uint16_t read_uint16_t(FILE* file) {
	fread(&stmp, sizeof(uint16_t), 1, file);
	return stmp;
}

static uint32_t read_uint32_t(FILE* file) {
	fread(&itmp, sizeof(uint32_t), 1, file);
	return itmp;
}

static uint8_t read_double(FILE* file) {
	fread(&dtmp, sizeof(double), 1, file);
	return dtmp;
}

void lit_save_chunk(LitChunk* chunk, FILE* file) {
	write_uint32_t(file, chunk->count);

	for (uint i = 0; i < chunk->count; i++) {
		write_uint8_t(file, chunk->code[i]);
	}

	write_uint32_t(file, chunk->line_count);

	for (uint i = 0; i < chunk->line_count; i++) {
		write_uint16_t(file, chunk->lines[i]);
	}

	write_uint32_t(file, chunk->constants.count);

	for (uint i = 0; i < chunk->constants.count; i++) {
		write_double(file, chunk->constants.values[i]);
	}
}

LitChunk* lit_load_chunk(LitState* state, FILE* file) {
	LitChunk* chunk = (LitChunk*) lit_reallocate(state, NULL, 0, sizeof(LitChunk));
	lit_init_chunk(chunk);
	
	uint count = read_uint32_t(file);
	chunk->code = (uint8_t*) lit_reallocate(state, NULL, 0, sizeof(uint8_t) * count);
	chunk->count = count;
	chunk->capacity = count;
	
	for (uint i = 0; i < count; i++) {
		chunk->code[i] = read_uint8_t(file);
	}

	count = read_uint32_t(file);
	chunk->lines = (uint16_t*) lit_reallocate(state, NULL, 0, sizeof(uint16_t) * count);
	chunk->line_count = count;
	chunk->line_capacity = count;

	for (uint i = 0; i < count; i++) {
		chunk->lines[i] = read_uint16_t(file);
	}

	count = read_uint32_t(file);
	chunk->constants.values = (LitValue*) lit_reallocate(state, NULL, 0, sizeof(LitValue) * count);
	chunk->constants.count = count;
	chunk->constants.capacity = count;

	for (uint i = 0; i < count; i++) {
		chunk->constants.values[i] = (LitValue) read_double(file);
	}
	
	return chunk;
}