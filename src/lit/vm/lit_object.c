#include <lit/vm/lit_object.h>
#include <lit/mem/lit_mem.h>
#include <lit/vm/lit_vm.h>

#include <memory.h>

LitFunction* lit_create_function(LitState* state) {
	LitFunction* function = ALLOCATE_OBJECT(state, LitFunction, OBJECT_FUNCTION);
	lit_init_chunk(&function->chunk);

	function->name = NULL;
	function->arg_count = 0;

	return function;
}

static LitString* allocate_string(LitState* state, char* chars, int length, uint32_t hash) {
	LitString* string = ALLOCATE_OBJECT(state, LitString, OBJECT_STRING);

	string->length = length;
	string->chars = chars;
	string->hash = hash;

	lit_table_set(state, &state->vm->strings, string, NULL_VAL);

	return string;
}

static uint32_t hash_string(const char* key, uint length) {
	uint32_t hash = 2166136261u;

	for (uint i = 0; i < length; i++) {
		hash ^= key[i];
		hash *= 16777619;
	}

	return hash;
}

LitString* lit_copy_string(LitState* state, const char* chars, uint length) {
	uint32_t hash = hash_string(chars, length);
	LitString* interned = lit_table_find_string(&state->vm->strings, chars, length, hash);

	if (interned != NULL) {
		return interned;
	}

	char* heap_chars = LIT_ALLOCATE(state, char, length + 1);

	memcpy(heap_chars, chars, length);
	heap_chars[length] = '\0';

	return allocate_string(state, heap_chars, length, hash);
}

LitObject* lit_allocate_object(LitState* state, size_t size, LitObjectType type) {
	LitObject* object = (LitObject*) lit_reallocate(state, NULL, 0, size);

	object->type = type;
	object->next = state->vm->objects;
	state->vm->objects = object;

	return object;
}

LitNative* lit_new_native(LitState* state, LitNativeFn function) {
	LitNative* native = ALLOCATE_OBJECT(state, LitNative, OBJECT_NATIVE);
	native->function = function;
	return native;
}