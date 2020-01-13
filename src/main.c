#include <lit/lit.h>
#include <lit/vm/lit_chunk.h>
#include <lit/debug/lit_debug.h>
#include <lit/vm/lit_vm.h>

#include <stdio.h>

int main(int argc, char* argv[]) {
	LitState* state = lit_new_state();

	/*LitChunk chunk;
	/*lit_init_chunk(&chunk);

	lit_write_chunk(state, &chunk, OP_CONSTANT, 1);
	lit_write_chunk(state, &chunk, lit_chunk_add_constant(state, &chunk, 6.9), 1);

	lit_write_chunk(state, &chunk, OP_CONSTANT, 1);
	lit_write_chunk(state, &chunk, lit_chunk_add_constant(state, &chunk, 42.0), 1);

	lit_write_chunk(state, &chunk, OP_ADD, 1);
	lit_write_chunk(state, &chunk, OP_RETURN, 2);*/

	// lit_interpret_chunk(state, &chunk);

	/*FILE* file = fopen("chunk.lbc", "wb");
	lit_save_chunk(&chunk, file);
	fclose(file);*/

	FILE* file = fopen("chunk.lbc", "r");
	LitChunk* chunk = lit_load_chunk(state, file);
	fclose(file);

	lit_disassemble_chunk(chunk, "Test Chunk");
	lit_free_chunk(state, chunk);

	lit_free_state(state);

	return 0;
}