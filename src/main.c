#include <lit/lit.h>
#include <lit/vm/lit_chunk.h>
#include <lit/debug/lit_debug.h>
#include <lit/vm/lit_vm.h>

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

int main(int argc, char* argv[]) {
	LitState* state = lit_new_state();

	save_chunk(state);
	load_chunk(state);

	lit_free_state(state);

	return 0;
}