#include "vm/lit_object.h"
#include "mem/lit_mem.h"
#include "vm/lit_vm.h"
#include "optimizer/lit_optimizer.h"

#include <memory.h>
#include <math.h>

bool lit_is_callable_function(LitValue value) {
	if (IS_OBJECT(value)) {
		LitObjectType type = OBJECT_TYPE(value);
		return type == OBJECT_CLOSURE || type == OBJECT_FUNCTION || type == OBJECT_NATIVE_FUNCTION || type == OBJECT_NATIVE_PRIMITIVE || type == OBJECT_NATIVE_METHOD || type == OBJECT_PRIMITIVE_METHOD;
	}

	return false;
}

LitString* lit_allocate_empty_string(LitState* state, uint length) {
	LitString* string = ALLOCATE_OBJECT(state, LitString, OBJECT_STRING);
	string->length = length;
	return string;
}

void lit_register_string(LitState* state, LitString* string) {
	lit_push_root(state, (LitObject*) string);
	lit_table_set(state, &state->vm->strings, string, NULL_VALUE);
	lit_pop_root(state);
}

static LitString* allocate_string(LitState* state, char* chars, uint length, uint32_t hash) {
	LitString* string = lit_allocate_empty_string(state, length);

	string->chars = chars;
	string->hash = hash;

	lit_register_string(state, string);

	return string;
}

uint32_t lit_hash_string(const char* key, uint length) {
	uint32_t hash = 2166136261u;

	for (uint i = 0; i < length; i++) {
		hash ^= key[i];
		hash *= 16777619;
	}

	return hash;
}

LitString* lit_take_string(LitState* state, const char* chars, uint length) {
	uint32_t hash = lit_hash_string(chars, length);
	LitString* interned = lit_table_find_string(&state->vm->strings, chars, length, hash);

	if (interned != NULL) {
		return interned;
	}

	return allocate_string(state, (char*) chars, length, hash);
}

LitString* lit_copy_string(LitState* state, const char* chars, uint length) {
	uint32_t hash = lit_hash_string(chars, length);
	LitString* interned = lit_table_find_string(&state->vm->strings, chars, length, hash);

	if (interned != NULL) {
		return interned;
	}

	char* heap_chars = LIT_ALLOCATE(state, char, length + 1);

	memcpy(heap_chars, chars, length);
	heap_chars[length] = '\0';

#ifdef LIT_LOG_ALLOCATION
	printf("Allocated new string '%s'\n", chars);
#endif

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
	bool was_allowed = state->allow_gc;
	state->allow_gc = false;

	va_list arg_list;

	va_start(arg_list, format);
	size_t total_length = 0;

	for (const char* c = format; *c != '\0'; c++) {
		switch (*c) {
			case '$': {
				const char* cc = va_arg(arg_list, const char*);

				if (cc != NULL) {
					total_length += strlen(cc);
					break;
				}

				goto default_ending;
			}

			case '@': {
				LitValue v = va_arg(arg_list, LitValue);
				LitString* ss = AS_STRING(v);

				if (ss != NULL) {
					total_length += ss->length;
					break;
				}

				goto default_ending;
			}

			case '#': {
				total_length += AS_STRING(lit_number_to_string(state, va_arg(arg_list, double)))->length;
				break;
			}

			default: {
				default_ending:
				total_length++;

				break;
			}
		}
	}

	va_end(arg_list);

	LitString* result = lit_allocate_empty_string(state, total_length);
	result->chars = LIT_ALLOCATE(state, char, total_length + 1);
	result->chars[total_length] = '\0';

	char* start = result->chars;
	va_start(arg_list, format);

	for (const char* c = format; *c != '\0'; c++) {
		switch (*c) {
			case '$': {
				const char* string = va_arg(arg_list, const char*);

				if (string != NULL) {
					size_t length = strlen(string);
					memcpy(start, string, length);
					start += length;

					break;
				}

				goto default_ending_copying;
			}

			case '@': {
				LitString* string = AS_STRING(va_arg(arg_list, LitValue));

				if (string != NULL) {
					memcpy(start, string->chars, string->length);
					start += string->length;

					break;
				}

				goto default_ending_copying;
			}

			case '#': {
				LitString* string = AS_STRING(lit_number_to_string(state, va_arg(arg_list, double)));
				memcpy(start, string->chars, string->length);
				start += string->length;

				break;
			}

			default: {
				default_ending_copying:
				*start++ = *c;

				break;
			}
		}
	}

	va_end(arg_list);

	result->hash = lit_hash_string(result->chars, result->length);
	lit_register_string(state, result);

	state->allow_gc = was_allowed;

	return OBJECT_VALUE(result);
}

LitObject* lit_allocate_object(LitState* state, size_t size, LitObjectType type) {
	LitObject* object = (LitObject*) lit_reallocate(state, NULL, 0, size);

	object->type = type;
	object->marked = false;
	object->next = state->vm->objects;

	state->vm->objects = object;

#ifdef LIT_LOG_ALLOCATION
	printf("%p allocate %ld for %s\n", (void*) object, size, lit_object_type_names[type]);
#endif

	return object;
}

LitFunction* lit_create_function(LitState* state, LitModule* module) {
	LitFunction* function = ALLOCATE_OBJECT(state, LitFunction, OBJECT_FUNCTION);
	lit_init_chunk(&function->chunk);

	function->name = NULL;
	function->arg_count = 0;
	function->upvalue_count = 0;
	function->max_slots = 0;
	function->module = module;
	function->vararg = false;

	return function;
}


LitValue lit_get_function_name(LitVm* vm, LitValue instance) {
	LitString* name = NULL;

	switch (OBJECT_TYPE(instance)) {
		case OBJECT_FUNCTION: {
			name = AS_FUNCTION(instance)->name;
			break;
		}

		case OBJECT_CLOSURE: {
			name = AS_CLOSURE(instance)->function->name;
			break;
		}

		case OBJECT_FIELD: {
			LitField* field = AS_FIELD(instance);

			if (field->getter != NULL) {
				return lit_get_function_name(vm, OBJECT_VALUE(field->getter));
			}

			return lit_get_function_name(vm, OBJECT_VALUE(field->setter));
		}

		case OBJECT_NATIVE_PRIMITIVE: {
			name = AS_NATIVE_PRIMITIVE(instance)->name;
			break;
		}

		case OBJECT_NATIVE_FUNCTION: {
			name = AS_NATIVE_FUNCTION(instance)->name;
			break;
		}

		case OBJECT_NATIVE_METHOD: {
			name = AS_NATIVE_METHOD(instance)->name;
			break;
		}

		case OBJECT_PRIMITIVE_METHOD: {
			name = AS_PRIMITIVE_METHOD(instance)->name;
			break;
		}

		case OBJECT_BOUND_METHOD: {
			return lit_get_function_name(vm, AS_BOUND_METHOD(instance)->method);
		}

		default: {
			break;
		}
	}

	if (name == NULL) {
		return OBJECT_VALUE(lit_string_format(vm->state, "function #", *((double*) AS_OBJECT(instance))));
	}

	return OBJECT_VALUE(lit_string_format(vm->state, "function @", OBJECT_VALUE(name)));
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

	lit_push_root(state, (LitObject*) closure);
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

LitNativeFunction* lit_create_native_function(LitState* state, LitNativeFunctionFn function, LitString* name) {
	LitNativeFunction* native = ALLOCATE_OBJECT(state, LitNativeFunction, OBJECT_NATIVE_FUNCTION);

	native->function = function;
	native->name = name;

	return native;
}

LitNativePrimitive* lit_create_native_primitive(LitState* state, LitNativePrimitiveFn function, LitString* name) {
	LitNativePrimitive* native = ALLOCATE_OBJECT(state, LitNativePrimitive, OBJECT_NATIVE_PRIMITIVE);

	native->function = function;
	native->name = name;

	return native;
}

LitNativeMethod* lit_create_native_method(LitState* state, LitNativeMethodFn method, LitString* name) {
	LitNativeMethod* native = ALLOCATE_OBJECT(state, LitNativeMethod, OBJECT_NATIVE_METHOD);

	native->method = method;
	native->name = name;

	return native;
}

LitPrimitiveMethod* lit_create_primitive_method(LitState* state, LitPrimitiveMethodFn method, LitString* name) {
	LitPrimitiveMethod* native = ALLOCATE_OBJECT(state, LitPrimitiveMethod, OBJECT_PRIMITIVE_METHOD);

	native->method = method;
	native->name = name;

	return native;
}

LitFiber* lit_create_fiber(LitState* state, LitModule* module, LitFunction* function) {
	// Allocate in advance, just in case GC is triggered
	uint stack_capacity = function == NULL ? 1 : (uint) lit_closest_power_of_two(function->max_slots + 1);
	LitValue* stack = LIT_ALLOCATE(state, LitValue, stack_capacity);

	LitCallFrame* frames = LIT_ALLOCATE(state, LitCallFrame, LIT_INITIAL_CALL_FRAMES);
	LitFiber* fiber = ALLOCATE_OBJECT(state, LitFiber, OBJECT_FIBER);

	if (module->main_fiber == NULL) {
		module->main_fiber = fiber;
	}

	fiber->stack = stack;
	fiber->stack_capacity = stack_capacity;
	fiber->stack_top = fiber->stack;

	fiber->frames = frames;
	fiber->frame_capacity = LIT_INITIAL_CALL_FRAMES;

	fiber->parent = NULL;
	fiber->frame_count = 1;
	fiber->arg_count = 0;
	fiber->module = module;
	fiber->catcher = false;
	fiber->error = NULL_VALUE;
	fiber->open_upvalues = NULL;
	fiber->abort = false;

	LitCallFrame* frame = &fiber->frames[0];

	frame->closure = NULL;
	frame->function = function;
	frame->slots = fiber->stack;
	frame->result_ignored = false;

	if (function != NULL) {
		frame->ip = function->chunk.code;
	}

	return fiber;
}

void lit_ensure_fiber_stack(LitState* state, LitFiber* fiber, uint needed) {
	if (fiber->stack_capacity >= needed) {
		return;
	}

	uint capacity = (uint) lit_closest_power_of_two((int) needed);
	LitValue* old_stack = fiber->stack;

	fiber->stack = (LitValue*) lit_reallocate(state, fiber->stack, sizeof(LitValue) * fiber->stack_capacity, sizeof(LitValue) * capacity);
	fiber->stack_capacity = capacity;

	if (fiber->stack != old_stack) {
		for (uint i = 0; i < fiber->frame_capacity; i++) {
			LitCallFrame* frame = &fiber->frames[i];
			frame->slots = fiber->stack + (frame->slots - old_stack);
		}

		for (LitUpvalue* upvalue = fiber->open_upvalues; upvalue != NULL; upvalue = upvalue->next) {
			upvalue->location = fiber->stack + (upvalue->location - old_stack);
		}

		fiber->stack_top = fiber->stack + (fiber->stack_top - old_stack);
	}
}

LitModule* lit_create_module(LitState* state, LitString* name) {
	LitModule* module = ALLOCATE_OBJECT(state, LitModule, OBJECT_MODULE);

	module->name = name;
	module->return_value = NULL_VALUE;
	module->main_function = NULL;
	module->privates = NULL;
	module->ran = false;
	module->main_fiber = NULL;
	module->private_count = 0;
	module->private_names = lit_create_map(state);

	return module;
}

LitClass* lit_create_class(LitState* state, LitString* name) {
	LitClass* klass = ALLOCATE_OBJECT(state, LitClass, OBJECT_CLASS);

	klass->name = name;
	klass->init_method = NULL;
	klass->super = NULL;

	lit_init_table(&klass->methods);
	lit_init_table(&klass->static_fields);

	return klass;
}

LitInstance* lit_create_instance(LitState* state, LitClass* klass) {
	LitInstance* instance = ALLOCATE_OBJECT(state, LitInstance, OBJECT_INSTANCE);

	instance->klass = klass;
	lit_init_table(&instance->fields);

	return instance;
}

LitBoundMethod* lit_create_bound_method(LitState* state, LitValue receiver, LitValue method) {
	LitBoundMethod* bound_method = ALLOCATE_OBJECT(state, LitBoundMethod, OBJECT_BOUND_METHOD);

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
	map->index_fn = NULL;

	return map;
}

bool lit_map_set(LitState* state, LitMap* map, LitString* key, LitValue value) {
	if (value == NULL_VALUE) {
		lit_map_delete(map, key);
		return false;
	}

	return lit_table_set(state, &map->values, key, value);
}

bool lit_map_get(LitMap* map, LitString* key, LitValue* value) {
	return lit_table_get(&map->values, key, value);
}

bool lit_map_delete(LitMap* map, LitString* key) {
	return lit_table_delete(&map->values, key);
}

void lit_map_add_all(LitState* state, LitMap* from, LitMap* to) {
	for (int i = 0; i <= from->values.capacity; i++) {
		LitTableEntry* entry = &from->values.entries[i];

		if (entry->key != NULL) {
			lit_table_set(state, &to->values, entry->key, entry->value);
		}
	}
}

LitUserdata* lit_create_userdata(LitState* state, size_t size) {
	LitUserdata* userdata = ALLOCATE_OBJECT(state, LitUserdata, OBJECT_USERDATA);

	if (size > 0) {
		userdata->data = lit_reallocate(state, NULL, 0, size);
	} else {
		userdata->data = NULL;
	}

	userdata->size = size;
	userdata->cleanup_fn = NULL;

	return userdata;
}

LitRange* lit_create_range(LitState* state, double from, double to) {
	LitRange* range = ALLOCATE_OBJECT(state, LitRange, OBJECT_RANGE);

	range->from = from;
	range->to = to;

	return range;
}

LitField* lit_create_field(LitState* state, LitObject* getter, LitObject* setter) {
	LitField* field = ALLOCATE_OBJECT(state, LitField, OBJECT_FIELD);

	field->getter = getter;
	field->setter = setter;

	return field;
}

LitReference* lit_create_reference(LitState* state, LitValue* slot) {
	LitReference* reference = ALLOCATE_OBJECT(state, LitReference, OBJECT_REFERENCE);
	reference->slot = slot;
	return reference;
}