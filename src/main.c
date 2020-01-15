#include <lit/vm/lit_chunk.h>
#include <lit/debug/lit_debug.h>
#include <lit/scanner/lit_scanner.h>

#include <stdlib.h>
#include <stdio.h>

static void save_chunk(LitState* state) {
	LitChunk chunk;
	lit_init_chunk(&chunk);

	lit_write_chunk(state, &chunk, OP_CONSTANT, 1);
	lit_write_chunk(state, &chunk, lit_chunk_add_constant(state, &chunk, 6.9), 1);

	lit_write_chunk(state, &chunk, OP_CONSTANT, 1);
	lit_write_chunk(state, &chunk, lit_chunk_add_constant(state, &chunk, 32.0), 1);

	lit_write_chunk(state, &chunk, OP_SUBTRACT, 2);
	lit_write_chunk(state, &chunk, OP_NEGATE, 3);
	lit_write_chunk(state, &chunk, OP_RETURN, 4);

	lit_disassemble_chunk(&chunk, "Test Chunk");

	FILE* file = fopen("chunk.lbc", "wb");
	lit_save_chunk(&chunk, file);
	fclose(file);
}

static void load_chunk(LitState* state) {
	FILE* file = fopen("chunk.lbc", "r");
	LitChunk* chunk = lit_load_chunk(state, file);
	fclose(file);

	lit_disassemble_chunk(chunk, "Test Chunk");
	lit_free_chunk(state, chunk);
}

static void load_and_run_chunk(LitState* state) {
	FILE* file = fopen("chunk.lbc", "r");
	LitChunk* chunk = lit_load_chunk(state, file);
	fclose(file);

	lit_interpret_chunk(state, chunk);
	lit_free_chunk(state, chunk);
}

static char* read_file(const char* path) {
	FILE* file = fopen(path, "rb");

	fseek(file, 0L, SEEK_END);
	size_t fileSize = ftell(file);
	rewind(file);

	char* buffer = (char*) malloc(fileSize + 1);
	size_t bytes_read = fread(buffer, sizeof(char), fileSize, file);
	buffer[bytes_read] = '\0';

	fclose(file);
	return buffer;
}

int main(int argc, char* argv[]) {
	char* source = read_file("test.lit");

	LitState* state = lit_new_state();
	lit_interpret(state, source);
	lit_free_state(state);

	free(source);

	return 0;
}