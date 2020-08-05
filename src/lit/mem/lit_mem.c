#include <lit/mem/lit_mem.h>
#include <lit/vm/lit_object.h>
#include <lit/vm/lit_vm.h>
#include <lit/emitter/lit_emitter.h>
#include <lit/parser/lit_parser.h>

#include <stdlib.h>
#include <stdio.h>

void* lit_reallocate(LitState* state, void* pointer, size_t old_size, size_t new_size) {
	state->bytes_allocated += (int64_t) new_size - (int64_t) old_size;

	if (new_size > old_size) {
#ifdef LIT_STRESS_TEST_GC
		lit_collect_garbage(state->vm);
#endif

		if (state->bytes_allocated > state->next_gc) {
			lit_collect_garbage(state->vm);
		}
	}

	if (new_size == 0) {
		free(pointer);
		return NULL;
	}

	void* ptr = realloc(pointer, new_size);

	if (ptr == NULL)  {
		lit_error(state, RUNTIME_ERROR, "Fatal error:\nOut of memory\nProgram terminated");
		exit(111);
	}

	return ptr;
}

void lit_free_object(LitState* state, LitObject* object) {
#ifdef LIT_LOG_GC
	printf("(");
	lit_print_value(OBJECT_VALUE(object));
	printf(") %p free %s\n", (void*) object, lit_object_type_names[object->type]);
#endif

	switch (object->type) {
		case OBJECT_STRING: {
			LitString* string = (LitString*) object;

			LIT_FREE_ARRAY(state, char, string->chars, string->length + 1);
			LIT_FREE(state, LitString, object);

			break;
		}

		case OBJECT_FUNCTION: {
			LitFunction* function = (LitFunction*) object;
			lit_free_chunk(state, &function->chunk);

			LIT_FREE(state, LitFunction, object);
			break;
		}

		case OBJECT_NATIVE_FUNCTION: {
			LIT_FREE(state, LitNativeFunction, object);
			break;
		}

		case OBJECT_NATIVE_PRIMITIVE: {
			LIT_FREE(state, LitNativePrimitive, object);
			break;
		}

		case OBJECT_NATIVE_METHOD: {
			LIT_FREE(state, LitNativeMethod, object);
			break;
		}

		case OBJECT_PRIMITIVE_METHOD: {
			LIT_FREE(state, LitPrimitiveMethod, object);
			break;
		}

		case OBJECT_FIBER: {
			LIT_FREE(state, LitFiber, object);
			break;
		}

		case OBJECT_MODULE: {
			LitModule* module = (LitModule*) object;

			LIT_FREE_ARRAY(state, LitValue, module->privates, module->private_names.count);
			lit_free_table(state, &module->private_names);
			LIT_FREE(state, LitModule, object);

			break;
		}

		case OBJECT_CLOSURE: {
			LitClosure* closure = (LitClosure*) object;

			LIT_FREE_ARRAY(state, LitUpvalue*, closure->upvalues, closure->upvalue_count);
			LIT_FREE(state, LitClosure, object);

			break;
		}

		case OBJECT_UPVALUE: {
			LIT_FREE(state, LitUpvalue, object);
			break;
		}

		case OBJECT_CLASS: {
			LitClass* klass = (LitClass*) object;

			lit_free_table(state, &klass->methods);
			lit_free_table(state, &klass->static_fields);

			LIT_FREE(state, LitClass, object);

			break;
		}

		case OBJECT_INSTANCE: {
			lit_free_table(state, &((LitInstance*) object)->fields);
			LIT_FREE(state, LitInstance, object);

			break;
		}

		case OBJECT_BOUND_METHOD: {
			LIT_FREE(state, LitBoundMethod, object);
			break;
		}

		case OBJECT_ARRAY: {
			lit_free_values(state, &((LitArray*) object)->values);
			LIT_FREE(state, LitArray, object);

			break;
		}

		case OBJECT_MAP: {
			lit_free_table(state, &((LitMap*) object)->values);
			LIT_FREE(state, LitMap, object);

			break;
		}

		case OBJECT_USERDATA: {
			LitUserdata* data = (LitUserdata*) object;

			if (data->cleanup_fn != NULL) {
				data->cleanup_fn(state, data);
			}

			lit_reallocate(state, data->data, data->size, 0);
			LIT_FREE(state, LitUserdata, object);

			break;
		}

		case OBJECT_RANGE: {
			LIT_FREE(state, LitRange, object);
			break;
		}

		case OBJECT_FIELD: {
			LIT_FREE(state, LitField, object);
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
		lit_free_object(state, object);
		object = next;
	}

	free(state->vm->gray_stack);
	state->vm->gray_capacity = 0;
}

void lit_mark_object(LitVm* vm, LitObject* object) {
	if (object == NULL || object->marked) {
		return;
	}

	object->marked = true;

#ifdef LIT_LOG_GC
	printf("%p mark ", (void*) object);
  lit_print_value(OBJECT_VALUE(object));
  printf("\n");

#endif

	if (vm->gray_capacity < vm->gray_count + 1) {
		vm->gray_capacity = LIT_GROW_CAPACITY(vm->gray_capacity);
		vm->gray_stack = realloc(vm->gray_stack, sizeof(LitObject*) * vm->gray_capacity);
	}

	vm->gray_stack[vm->gray_count++] = object;
}

void lit_mark_value(LitVm* vm, LitValue value) {
	if (IS_OBJECT(value)) {
		lit_mark_object(vm, AS_OBJECT(value));
	}
}

static void mark_roots(LitVm* vm) {
	LitFiber* fiber = vm->fiber;
	LitState* state = vm->state;

	while (fiber != NULL) {
		lit_mark_object(vm, (LitObject *) fiber);

		for (LitValue *slot = fiber->stack; slot < fiber->stack_top; slot++) {
			lit_mark_value(vm, *slot);
		}

		for (uint i = 0; i < fiber->frame_count; i++) {
			LitCallFrame *frame = &fiber->frames[i];

			if (frame->closure != NULL) {
				lit_mark_object(vm, (LitObject *) frame->closure);
			} else {
				lit_mark_object(vm, (LitObject *) frame->function);
			}
		}

		fiber = fiber->parent;
	}

	for (LitUpvalue* upvalue = vm->open_upvalues; upvalue != NULL; upvalue = upvalue->next) {
		lit_mark_object(vm, (LitObject*) upvalue);
	}

	for (uint i = 0; i < state->root_count; i++) {
		lit_mark_value(vm, state->roots[i]);
	}

	lit_mark_object(vm, (LitObject *) state->api_module);

	lit_mark_object(vm, (LitObject *) state->class_class);
	lit_mark_object(vm, (LitObject *) state->object_class);
	lit_mark_object(vm, (LitObject *) state->number_class);
	lit_mark_object(vm, (LitObject *) state->string_class);
	lit_mark_object(vm, (LitObject *) state->bool_class);
	lit_mark_object(vm, (LitObject *) state->function_class);
	lit_mark_object(vm, (LitObject *) state->fiber_class);
	lit_mark_object(vm, (LitObject *) state->module_class);
	lit_mark_object(vm, (LitObject *) state->array_class);
	lit_mark_object(vm, (LitObject *) state->map_class);

	lit_mark_table(vm, &vm->globals);
	lit_mark_table(vm, &vm->modules);
}

static void mark_array(LitVm* vm, LitValues* array) {
	for (uint i = 0; i < array->count; i++) {
		lit_mark_value(vm, array->values[i]);
	}
}

static void blacken_object(LitVm* vm, LitObject* object) {
#ifdef LIT_LOG_GC
	printf("%p blacken ", (void*) object);
  lit_print_value(OBJECT_VALUE(object));
  printf("\n");
#endif

	switch (object->type) {
		case OBJECT_NATIVE_FUNCTION:
		case OBJECT_NATIVE_PRIMITIVE:
		case OBJECT_NATIVE_METHOD:
		case OBJECT_PRIMITIVE_METHOD:
		case OBJECT_RANGE:
		case OBJECT_USERDATA:
		case OBJECT_STRING: {
			break;
		}

		case OBJECT_FUNCTION: {
			LitFunction* function = (LitFunction*) object;

			lit_mark_object(vm, (LitObject*) function->name);
			mark_array(vm, &function->chunk.constants);

			break;
		}

		case OBJECT_FIBER: {
			LitFiber* fiber = (LitFiber*) object;

			lit_mark_object(vm, (LitObject *) fiber->module);
			lit_mark_object(vm, (LitObject *) fiber->parent);

			break;
		}

		case OBJECT_MODULE: {
			LitModule* module = (LitModule*) object;

			lit_mark_value(vm, module->return_value);

			lit_mark_object(vm, (LitObject *) module->name);
			lit_mark_object(vm, (LitObject *) module->main_function);

			for (uint i = 0; i < module->private_names.count; i++) {
				lit_mark_value(vm, module->privates[i]);
			}

			lit_mark_table(vm, &module->private_names);
			break;
		}

		case OBJECT_CLOSURE: {
			LitClosure* closure = (LitClosure*) object;
			lit_mark_object(vm, (LitObject*) closure->function);

			// Check for NULL is needed for a really specific gc-case
			if (closure->upvalues != NULL) {
				for (uint i = 0; i < closure->upvalue_count; i++) {
					lit_mark_object(vm, (LitObject *) closure->upvalues[i]);
				}
			}

			break;
		}

		case OBJECT_UPVALUE: {
			lit_mark_value(vm, ((LitUpvalue*) object)->closed);
			break;
		}

		case OBJECT_CLASS: {
			LitClass* klass = (LitClass*) object;

			lit_mark_object(vm, (LitObject *) klass->name);
			lit_mark_object(vm, (LitObject *) klass->super);

			lit_mark_table(vm, &klass->methods);
			lit_mark_table(vm, &klass->static_fields);

			break;
		}

		case OBJECT_INSTANCE: {
			LitInstance* instance = (LitInstance*) object;

			lit_mark_object(vm, (LitObject *) instance->klass);
			lit_mark_table(vm, &instance->fields);

			break;
		}

		case OBJECT_BOUND_METHOD: {
			LitBoundMethod* bound_method = (LitBoundMethod*) object;

			lit_mark_value(vm, bound_method->receiver);
			lit_mark_value(vm, bound_method->method);

			break;
		}

		case OBJECT_ARRAY: {
			mark_array(vm, &((LitArray*) object)->values);
			break;
		}

		case OBJECT_MAP: {
			lit_mark_table(vm, &((LitMap*) object)->values);
			break;
		}

		case OBJECT_FIELD: {
			LitField* field = (LitField*) object;

			lit_mark_object(vm, (LitObject *) field->getter);
			lit_mark_object(vm, (LitObject *) field->setter);

			break;
		}

		default: {
			UNREACHABLE
		}
	}
}

static void trace_references(LitVm* vm) {
	while (vm->gray_count > 0) {
		LitObject* object = vm->gray_stack[--vm->gray_count];
		blacken_object(vm, object);
	}
}

static void sweep(LitVm* vm) {
	LitObject* previous = NULL;
	LitObject* object = vm->objects;

	while (object != NULL) {
		if (object->marked) {
			object->marked = false;
			previous = object;
			object = object->next;
		} else {
			LitObject* unreached = object;
			object = object->next;

			if (previous != NULL) {
				previous->next = object;
			} else {
				vm->objects = object;
			}

			lit_free_object(vm->state, unreached);
		}
	}
}

void lit_collect_garbage(LitVm* vm) {
	if (!vm->state->allow_gc) {
		return;
	}

	vm->state->allow_gc = false;

#ifdef LIT_LOG_GC
	printf("-- gc begin\n");
	uint64_t before = vm->state->bytes_allocated;
#endif

	mark_roots(vm);
	trace_references(vm);
	lit_table_remove_white(&vm->strings);
	sweep(vm);

	vm->state->next_gc = vm->state->bytes_allocated * LIT_GC_HEAP_GROW_FACTOR;
	vm->state->allow_gc = true;

#ifdef LIT_LOG_GC
	printf("-- gc end\n");
	printf("   collected %ld bytes (from %ld to %ld) next at %ld\n", before - vm->state->bytes_allocated, before, vm->state->bytes_allocated, vm->state->next_gc);
#endif
}