#include <lit/mem/lit_mem.h>
#include <stdlib.h>

void* lit_reallocate(LitState* state, void* pointer, size_t old_size, size_t new_size) {
	state->bytes_allocated += new_size - old_size;

	if (new_size == 0) {
		free(pointer);
		return NULL;
	}

	return realloc(pointer, new_size);
}