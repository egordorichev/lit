#include <lit/mem/lit_mem.h>
#include <stdlib.h>
#include <stdio.h>
#include <lit/vm/lit_object.h>

void* lit_reallocate(LitState* state, void* pointer, size_t old_size, size_t new_size) {
	state->bytes_allocated += new_size - old_size;

	if (new_size == 0) {
		free(pointer);
		return NULL;
	}

	void* ptr = realloc(pointer, new_size);

	if (ptr == NULL)  {
		lit_error(state, RUNTIME_ERROR, 0, "Fatal error:\nOut of memory\nProgram terminated");
		exit(111);
	}

	return ptr;
}

static void free_object(LitState* state, LitObject* object) {
	switch (object->type) {
		case OBJECT_STRING: {
			LitString* string = (LitString*) object;

			LIT_FREE_ARRAY(state, char, string->chars, string->length + 1);
			LIT_FREE(state, LitString, object);

			break;
		}

		default: {
			UNREACHABLE
		}
	}
}

void lit_free_objects(LitState* state, LitObject* objects) {
	LitObject* object = objects;

	while (object != NULL) {
		LitObject* next = object->next;
		free_object(state, object);
		object = next;
	}
}