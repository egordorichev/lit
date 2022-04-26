#include "lit/mem/lit_mem.h"
#include "lit/vm/lit_object.h"
#include "lit/vm/lit_vm.h"
#include "lit/emitter/lit_emitter.h"
#include "lit/parser/lit_parser.h"
#include "lit/preprocessor/lit_preprocessor.h"

#include <stdlib.h>
#include <time.h>
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
#ifdef LIT_LOG_ALLOCATION
	printf("(");
	// Print simplified objects to avoid crashes due to their references being freed first
	switch (object->type) {
		case OBJECT_REFERENCE: printf("reference"); break;
		case OBJECT_UPVALUE: printf("upvalue"); break;
		default: 	lit_print_value(OBJECT_VALUE(object));
	}
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
			LitFiber* fiber = (LitFiber*) object;

			LIT_FREE_ARRAY(state, LitCallFrame, fiber->frames, fiber->frame_capacity);
			LIT_FREE_ARRAY(state, LitValue, fiber->registers, fiber->registers_allocated);
			LIT_FREE(state, LitFiber, object);

			break;
		}

		case OBJECT_MODULE: {
			LitModule* module = (LitModule*) object;

			LIT_FREE_ARRAY(state, LitValue, module->privates, module->private_count);
			LIT_FREE(state, LitModule, object);

			break;
		}

		case OBJECT_CLOSURE: {
			LitClosure* closure = (LitClosure*) object;

			LIT_FREE_ARRAY(state, LitUpvalue*, closure->upvalues, closure->upvalue_count);
			LIT_FREE(state, LitClosure, object);

			break;
		}

		case OBJECT_CLOSURE_PROTOTYPE: {
			LitClosurePrototype* closure_prototype = (LitClosurePrototype*) object;

			LIT_FREE_ARRAY(state, uint8_t, closure_prototype->indexes, closure_prototype->upvalue_count);
			LIT_FREE_ARRAY(state, bool, closure_prototype->local, closure_prototype->upvalue_count);

			LIT_FREE(state, LitClosurePrototype, object);

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

		case OBJECT_VARARG_ARRAY: {
			lit_free_values(state, &((LitVarargArray*) object)->array.values);
			LIT_FREE(state, LitVarargArray, object);

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
				data->cleanup_fn(state, data, false);
			}

			if (data->size > 0) {
				lit_reallocate(state, data->data, data->size, 0);
			}

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

		case OBJECT_REFERENCE: {
			LIT_FREE(state, LitReference, object);
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

#ifdef LIT_LOG_MARKING
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
	LitState* state = vm->state;

	for (uint i = 0; i < state->root_count; i++) {
		lit_mark_value(vm, state->roots[i]);
	}

	lit_mark_object(vm, (LitObject*) vm->fiber);

	lit_mark_object(vm, (LitObject*) state->class_class);
	lit_mark_object(vm, (LitObject*) state->object_class);
	lit_mark_object(vm, (LitObject*) state->number_class);
	lit_mark_object(vm, (LitObject*) state->string_class);
	lit_mark_object(vm, (LitObject*) state->bool_class);
	lit_mark_object(vm, (LitObject*) state->function_class);
	lit_mark_object(vm, (LitObject*) state->fiber_class);
	lit_mark_object(vm, (LitObject*) state->module_class);
	lit_mark_object(vm, (LitObject*) state->array_class);
	lit_mark_object(vm, (LitObject*) state->map_class);
	lit_mark_object(vm, (LitObject*) state->range_class);

	lit_mark_object(vm, (LitObject*) state->api_name);
	lit_mark_object(vm, (LitObject*) state->api_function);

	lit_mark_table(vm, &state->preprocessor->defined);

	lit_mark_table(vm, &vm->modules->values);
	lit_mark_table(vm, &vm->globals->values);
}

static void mark_array(LitVm* vm, LitValues* array) {
	for (uint i = 0; i < array->count; i++) {
		lit_mark_value(vm, array->values[i]);
	}
}

static void blacken_object(LitVm* vm, LitObject* object) {
#ifdef LIT_LOG_BLACKING
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
		case OBJECT_STRING: {
			break;
		}

		case OBJECT_USERDATA: {
			LitUserdata* data = (LitUserdata*) object;

			if (data->cleanup_fn != NULL) {
				data->cleanup_fn(vm->state, data, true);
			}

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

			for (uint i = 0; i < fiber->registers_allocated; i++) {
				lit_mark_value(vm, fiber->registers[i]);
			}

			for (uint i = 0; i < fiber->frame_count; i++) {
				LitCallFrame *frame = &fiber->frames[i];

				if (frame->closure != NULL) {
					lit_mark_object(vm, (LitObject*) frame->closure);
				} else {
					lit_mark_object(vm, (LitObject*) frame->function);
				}
			}

			for (LitUpvalue* upvalue = fiber->open_upvalues; upvalue != NULL; upvalue = upvalue->next) {
				lit_mark_object(vm, (LitObject*) upvalue);
			}

			lit_mark_value(vm, fiber->error);
			lit_mark_object(vm, (LitObject*) fiber->module);
			lit_mark_object(vm, (LitObject*) fiber->parent);

			break;
		}

		case OBJECT_MODULE: {
			LitModule* module = (LitModule*) object;

			lit_mark_value(vm, module->return_value);

			lit_mark_object(vm, (LitObject*) module->name);
			lit_mark_object(vm, (LitObject*) module->main_function);
			lit_mark_object(vm, (LitObject*) module->main_fiber);
			lit_mark_object(vm, (LitObject*) module->private_names);

			for (uint i = 0; i < module->private_count; i++) {
				lit_mark_value(vm, module->privates[i]);
			}

			break;
		}

		case OBJECT_CLOSURE: {
			LitClosure* closure = (LitClosure*) object;
			lit_mark_object(vm, (LitObject*) closure->function);

			// Check for NULL is needed for a really specific gc-case
			if (closure->upvalues != NULL) {
				for (uint i = 0; i < closure->upvalue_count; i++) {
					lit_mark_object(vm, (LitObject*) closure->upvalues[i]);
				}
			}

			break;
		}

		case OBJECT_CLOSURE_PROTOTYPE: {
			lit_mark_object(vm, (LitObject*) ((LitClosure*) object)->function);
			break;
		}

		case OBJECT_UPVALUE: {
			lit_mark_value(vm, ((LitUpvalue*) object)->closed);
			break;
		}

		case OBJECT_CLASS: {
			LitClass* klass = (LitClass*) object;

			lit_mark_object(vm, (LitObject*) klass->name);
			lit_mark_object(vm, (LitObject*) klass->super);

			lit_mark_table(vm, &klass->methods);
			lit_mark_table(vm, &klass->static_fields);

			break;
		}

		case OBJECT_INSTANCE: {
			LitInstance* instance = (LitInstance*) object;

			lit_mark_object(vm, (LitObject*) instance->klass);
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

		case OBJECT_VARARG_ARRAY: {
			mark_array(vm, &((LitVarargArray*) object)->array.values);
			break;
		}

		case OBJECT_MAP: {
			lit_mark_table(vm, &((LitMap*) object)->values);
			break;
		}

		case OBJECT_FIELD: {
			LitField* field = (LitField*) object;

			lit_mark_object(vm, (LitObject*) field->getter);
			lit_mark_object(vm, (LitObject*) field->setter);

			break;
		}

		case OBJECT_REFERENCE: {
			lit_mark_value(vm, *((LitReference*) object)->slot);
			break;
		}

		default: {
			lit_runtime_error_exiting(vm, "Unknown object with type %i", object->type);
			break;
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

uint64_t lit_collect_garbage(LitVm* vm) {
	if (!vm->state->allow_gc) {
		return 0;
	}

	vm->state->allow_gc = false;
	uint64_t before = vm->state->bytes_allocated;

#ifdef LIT_LOG_GC
	printf("-- gc begin\n");
	clock_t t = clock();
#endif

	mark_roots(vm);
	trace_references(vm);
	lit_table_remove_white(&vm->strings);
	sweep(vm);

	vm->state->next_gc = vm->state->bytes_allocated * LIT_GC_HEAP_GROW_FACTOR;
	vm->state->allow_gc = true;

	uint64_t collected = before - vm->state->bytes_allocated;

#ifdef LIT_LOG_GC
	printf("-- gc end. Collected %imb (%ib) in %gms\n", ((int) ((collected / 1024.0 + 0.5) / 10)) * 10, collected, (double) (clock() - t) / CLOCKS_PER_SEC * 1000);
#endif

	return collected;
}

// http://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2Float
int lit_closest_power_of_two(int n) {
	n--;
	n |= n >> 1;
	n |= n >> 2;
	n |= n >> 4;
	n |= n >> 8;
	n |= n >> 16;
	n++;

	return n;
}