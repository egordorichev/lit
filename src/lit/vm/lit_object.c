#include <lit/vm/lit_object.h>
#include <lit/mem/lit_mem.h>
#include <lit/vm/lit_vm.h>

#include <memory.h>

static LitString* allocate_string(LitState* state, char* chars, int length) {
	LitString* string = ALLOCATE_OBJECT(state, LitString, OBJECT_STRING);

	string->length = length;
	string->chars = chars;

	return string;
}

LitString* lit_copy_string(LitState* state, const char* chars, uint length) {
	char* heap_chars = LIT_ALLOCATE(state, char, length + 1);
	memcpy(heap_chars, chars, length);
	heap_chars[length] = '\0';

	return allocate_string(state, heap_chars, length);
}


LitObject* lit_allocate_object(LitState* state, size_t size, LitObjectType type) {
	LitObject* object = (LitObject*) lit_reallocate(state, NULL, 0, size);

	object->type = type;
	object->next = state->vm->objects;
	state->vm->objects = object;

	return object;
}