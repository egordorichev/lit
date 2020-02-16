#include <lit/vm/lit_object.h>
#include <lit/mem/lit_mem.h>
#include <lit/vm/lit_vm.h>

#include <memory.h>

static LitString* allocate_string(LitState* state, char* chars, int length, uint32_t hash) {
	LitString* string = ALLOCATE_OBJECT(state, LitString, OBJECT_STRING);

	string->length = length;
	string->chars = chars;
	string->hash = hash;

	lit_push_root(state, (LitObject *) string);
	lit_table_set(state, &state->vm->strings, string, NULL_VALUE);
	lit_pop_root(state);

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
	object->marked = false;
	object->next = state->vm->objects;

	state->vm->objects = object;

#ifdef LIT_LOG_GC
	printf("%p allocate %ld for %s\n", (void*) object, size, lit_object_type_names[type]);
#endif

	return object;
}

LitFunction* lit_create_function(LitState* state) {
	LitFunction* function = ALLOCATE_OBJECT(state, LitFunction, OBJECT_FUNCTION);
	lit_init_chunk(&function->chunk);

	function->name = NULL;
	function->arg_count = 0;
	function->upvalue_count = 0;

	return function;
}

LitUpvalue* lit_create_upvalue(LitState* state, LitValue* slot) {
	LitUpvalue* upvalue = ALLOCATE_OBJECT(state, LitUpvalue, OBJECT_UPVALUE);

	upvalue->location = slot;
	upvalue->closed = NULL_VALUE;
	upvalue->next = NULL;

	return upvalue;
}

LitClosure* lit_create_closure(LitState* state, LitFunction* function) {
	LitClosure* closure = ALLOCATE_OBJECT(state, LitClosure, OBJECT_CLOSURE);

	lit_push_root(state, (LitObject *) closure);
	LitUpvalue** upvalues = LIT_ALLOCATE(state, LitUpvalue*, function->upvalue_count);
	lit_pop_root(state);

	for (uint i = 0; i < function->upvalue_count; i++) {
		upvalues[i] = NULL;
	}

	closure->function = function;
	closure->upvalues = upvalues;
	closure->upvalue_count = function->upvalue_count;

	return closure;
}

LitNative* lit_create_native(LitState* state, LitNativeFn function) {
	LitNative* native = ALLOCATE_OBJECT(state, LitNative, OBJECT_NATIVE);
	native->function = function;
	return native;
}

LitFiber* lit_create_fiber(LitState* state, LitModule* module, LitFunction* function) {
	LitFiber* fiber = ALLOCATE_OBJECT(state, LitFiber, OBJECT_FIBER);

	fiber->stack_top = fiber->stack;
	fiber->parent = NULL;
	fiber->frame_count = 1;
	fiber->module = module;

	LitCallFrame* frame = &fiber->frames[0];

	frame->function = function;
	frame->ip = function->chunk.code;
	frame->slots = fiber->stack;

	return fiber;
}

LitModule* lit_create_module(LitState* state, LitString* name) {
	LitModule* module = ALLOCATE_OBJECT(state, LitModule, OBJECT_MODULE);

	module->name = name;
	module->return_value = NULL_VALUE;
	module->main_function = NULL;
	module->privates = NULL;
	module->privates_count = 0;

	return module;
}

LitClass* lit_create_class(LitState* state, LitString* name) {
	LitClass* klass = ALLOCATE_OBJECT(state, LitClass, OBJECT_CLASS);
	klass->name = name;

	return klass;
}