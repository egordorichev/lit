#include <lit/vm/lit_object.h>
#include <lit/mem/lit_mem.h>
#include <lit/vm/lit_vm.h>

#include <memory.h>
#include <math.h>

static LitString* allocate_empty_string(LitState* state, uint length) {
	LitString* string = ALLOCATE_OBJECT(state, LitString, OBJECT_STRING);
	string->length = length;
	return string;
}

static void register_string(LitState* state, LitString* string) {
	lit_push_root(state, (LitObject *) string);
	lit_table_set(state, &state->vm->strings, string, NULL_VALUE);
	lit_pop_root(state);
}

static LitString* allocate_string(LitState* state, char* chars, uint length, uint32_t hash) {
	LitString* string = allocate_empty_string(state, length);

	string->chars = chars;
	string->hash = hash;

	register_string(state, string);

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

LitString* lit_take_string(LitState* state, const char* chars, uint length) {
	uint32_t hash = hash_string(chars, length);
	LitString* interned = lit_table_find_string(&state->vm->strings, chars, length, hash);

	if (interned != NULL) {
		return interned;
	}

	return allocate_string(state, (char*) chars, length, hash);
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

LitValue lit_number_to_string(LitState* state, double value) {
	if (isnan(value)) {
		return OBJECT_CONST_STRING(state, "nan");
	}

	if (isinf(value)) {
		if (value > 0.0) {
			return OBJECT_CONST_STRING(state, "infinity");
		} else {
			return OBJECT_CONST_STRING(state, "-infinity");
		}
	}

	char buffer[24];
	int length = sprintf(buffer, "%.14g", value);

	return OBJECT_VALUE(lit_copy_string(state, buffer, length));
}

LitValue lit_string_format(LitState* state, const char* format, ...) {
	va_list arg_list;

	va_start(arg_list, format);
	size_t total_length = 0;

	for (const char* c = format; *c != '\0'; c++) {
		switch (*c) {
			case '$': {
				total_length += strlen(va_arg(arg_list, const char*));
				break;
			}

			case '@': {
				total_length += AS_STRING(va_arg(arg_list, LitValue))->length;
				break;
			}

			default: {
				total_length++;
			}
		}
	}

	va_end(arg_list);
	LitString* result = allocate_empty_string(state, total_length);
	result->chars = LIT_ALLOCATE(state, char, total_length + 1);
	result->chars[total_length] = '\0';
	va_start(arg_list, format);

	char* start = result->chars;

	for (const char* c = format; *c != '\0'; c++) {
		switch (*c) {
			case '$': {
				const char* string = va_arg(arg_list, const char*);
				size_t length = strlen(string);
				memcpy(start, string, length);
				start += length;

				break;
			}

			case '@': {
				LitString* string = AS_STRING(va_arg(arg_list, LitValue));
				memcpy(start, string->chars, string->length);
				start += string->length;

				break;
			}

			default: {
				*start++ = *c;
			}
		}
	}

	va_end(arg_list);

	result->hash = hash_string(result->chars, result->length);
	register_string(state, result);

	return OBJECT_VALUE(result);
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

LitNativeFunction* lit_create_native_function(LitState* state, LitNativeFunctionFn function) {
	LitNativeFunction* native = ALLOCATE_OBJECT(state, LitNativeFunction, OBJECT_NATIVE_FUNCTION);
	native->function = function;
	return native;
}

LitNativeMethod* lit_create_native_method(LitState* state, LitNativeMethodFn method) {
	LitNativeMethod* native = ALLOCATE_OBJECT(state, LitNativeMethod, OBJECT_NATIVE_METHOD);
	native->method = method;
	return native;
}

LitFiber* lit_create_fiber(LitState* state, LitModule* module, LitFunction* function) {
	LitFiber* fiber = ALLOCATE_OBJECT(state, LitFiber, OBJECT_FIBER);

	fiber->stack_top = fiber->stack;
	fiber->parent = NULL;
	fiber->frame_count = 1;
	fiber->module = module;

	for (uint i = 0; i < LIT_CALL_FRAMES_MAX; i++) {
		fiber->frames[i].closure = NULL;
	}

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
	klass->init_method = NULL;
	klass->super = NULL;

	lit_init_table(&klass->methods);
	lit_init_table(&klass->static_methods);

	return klass;
}

LitInstance* lit_create_instance(LitState* state, LitClass* klass) {
	LitInstance* instance = ALLOCATE_OBJECT(state, LitInstance, OBJECT_INSTANCE);

	instance->klass = klass;
	lit_init_table(&instance->fields);

	return instance;
}

LitBoundMethod* lit_create_bound_method(LitState* state, LitValue receiver, LitFunction* method) {
	LitBoundMethod* bound_method = ALLOCATE_OBJECT(state, LitBoundMethod, OBJECT_BOUND_METHOD);

	bound_method->receiver = receiver;
	bound_method->method = method;

	return bound_method;
}

LitNativeBoundMethod* lit_create_native_bound_method(LitState* state, LitValue receiver, LitNativeMethod* method) {
	LitNativeBoundMethod* bound_method = ALLOCATE_OBJECT(state, LitNativeBoundMethod, OBJECT_NATIVE_BOUND_METHOD);

	bound_method->receiver = receiver;
	bound_method->method = method;

	return bound_method;
}

LitArray* lit_create_array(LitState* state) {
	LitArray* array = ALLOCATE_OBJECT(state, LitArray, OBJECT_ARRAY);
	lit_init_values(&array->values);
	return array;
}

LitMap* lit_create_map(LitState* state) {
	LitMap* map = ALLOCATE_OBJECT(state, LitMap, OBJECT_MAP);
	lit_init_table(&map->values);
	return map;
}

LitUserdata* lit_create_userdata(LitState* state, size_t size) {
	LitUserdata* userdata = ALLOCATE_OBJECT(state, LitUserdata, OBJECT_USERDATA);

	userdata->data = lit_reallocate(state, NULL, 0, size);
	userdata->size = size;

	return userdata;
}