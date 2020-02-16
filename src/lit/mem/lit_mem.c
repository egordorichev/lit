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
		lit_error(state, RUNTIME_ERROR, 0, "Fatal error:\nOut of memory\nProgram terminated");
		exit(111);
	}

	return ptr;
}

static void free_object(LitState* state, LitObject* object) {
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

		case OBJECT_NATIVE: {
			LIT_FREE(state, LitNative, object);
			break;
		}

		case OBJECT_FIBER: {
			LIT_FREE(state, LitFiber, object);
			break;
		}

		case OBJECT_MODULE: {
			LitModule* module = (LitModule*) object;
			LitValue value = module->return_value;

			if (IS_OBJECT(value) && !IS_STRING(value)) {
				free_object(state, AS_OBJECT(value));
			}

			LIT_FREE_ARRAY(state, LitValue, module->privates, module->privates_count);
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

	if (fiber != NULL) {
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
	}

	for (LitUpvalue* upvalue = vm->open_upvalues; upvalue != NULL; upvalue = upvalue->next) {
		lit_mark_object(vm, (LitObject*) upvalue);
	}

	for (uint i = 0; i < vm->state->root_count; i++) {
		lit_mark_value(vm, vm->state->roots[i]);
	}

	lit_mark_table(vm, &vm->globals);
	lit_mark_table(vm, &vm->modules);

	lit_mark_parser_roots(vm->state->parser);
	lit_mark_emitter_roots(vm->state->emitter);
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
		case OBJECT_NATIVE:
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

			for (uint i = 0; i < module->privates_count; i++) {
				lit_mark_value(vm, module->privates[i]);
			}

			break;
		}

		case OBJECT_CLOSURE: {
			LitClosure* closure = (LitClosure*) object;
			lit_mark_object(vm, (LitObject*) closure->function);

			for (uint i = 0; i < closure->upvalue_count; i++) {
				lit_mark_object(vm, (LitObject*) closure->upvalues[i]);
			}

			break;
		}

		case OBJECT_UPVALUE: {
			lit_mark_value(vm, ((LitUpvalue*) object)->closed);
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

			free_object(vm->state, unreached);
		}
	}
}

void lit_collect_garbage(LitVm* vm) {
#ifdef LIT_LOG_GC
	printf("-- gc begin\n");
	uint64_t before = vm->state->bytes_allocated;
#endif

	mark_roots(vm);
	trace_references(vm);
	lit_table_remove_white(&vm->strings);
	sweep(vm);

	vm->state->next_gc = vm->state->bytes_allocated * LIT_GC_HEAP_GROW_FACTOR;

#ifdef LIT_LOG_GC
	printf("-- gc end\n");
	printf("   collected %ld bytes (from %ld to %ld) next at %ld\n", before - vm->state->bytes_allocated, before, vm->state->bytes_allocated, vm->state->next_gc);
#endif
}