#include <lit/api/lit_api.h>
#include <lit/vm/lit_vm.h>
#include <lit/vm/lit_chunk.h>
#include <lit/vm/lit_object.h>
#include <lit/debug/lit_debug.h>

#include <string.h>
#include <math.h>

void lit_init_api(LitState* state) {
	state->api_name = lit_copy_string(state, "c", 1);
	state->api_function = NULL;
	state->api_string_function = NULL;
	state->api_fiber = NULL;
}

void lit_free_api(LitState* state) {
	state->api_name = NULL;
	state->api_function = NULL;
	state->api_string_function = NULL;
	state->api_fiber = NULL;
}

LitValue lit_get_global(LitState* state, LitString* name) {
	LitValue global;

	if (!lit_table_get(&state->vm->globals->values, name, &global)) {
		return NULL_VALUE;
	}

	return global;
}

LitFunction* lit_get_global_function(LitState* state, LitString* name) {
	LitValue function = lit_get_global(state, name);

	if (IS_FUNCTION(function)) {
		return AS_FUNCTION(function);
	}

	return NULL;
}

void lit_set_global(LitState* state, LitString* name, LitValue value) {
	lit_push_root(state, (LitObject*) name);
	lit_push_value_root(state, value);
	lit_table_set(state, &state->vm->globals->values, name, value);
	lit_pop_roots(state, 2);
}

bool lit_global_exists(LitState* state, LitString* name) {
	LitValue global;
	return lit_table_get(&state->vm->globals->values, name, &global);
}

void lit_define_native(LitState* state, const char* name, LitNativeFunctionFn native) {
	lit_push_root(state, (LitObject*) CONST_STRING(state, name));
	lit_push_root(state, (LitObject*) lit_create_native_function(state, native, AS_STRING(lit_peek_root(state, 0))));
	lit_table_set(state, &state->vm->globals->values, AS_STRING(lit_peek_root(state, 1)), lit_peek_root(state, 0));
	lit_pop_roots(state, 2);
}

void lit_define_native_primitive(LitState* state, const char* name, LitNativePrimitiveFn native) {
	lit_push_root(state, (LitObject*) CONST_STRING(state, name));
	lit_push_root(state, (LitObject*) lit_create_native_primitive(state, native, AS_STRING(lit_peek_root(state, 0))));
	lit_table_set(state, &state->vm->globals->values, AS_STRING(lit_peek_root(state, 1)), lit_peek_root(state, 0));
	lit_pop_roots(state, 2);
}

LitInterpretResult lit_call(LitState* state, LitModule* module, LitValue callee, LitValue* arguments, uint8_t argument_count) {
	LitVm* vm = state->vm;
	LitFiber* fiber = state->api_fiber;

	if (fiber == NULL) {
		fiber = lit_create_fiber(state, module, NULL);
		fiber->frame_count = 0;
	} else {
		// Make it busy
		state->api_fiber = NULL;
	}

	if (fiber->frame_count == LIT_CALL_FRAMES_MAX) {
		lit_runtime_error(vm, "Stack overflow");
		return (LitInterpretResult) { INTERPRET_RUNTIME_ERROR, NULL_VALUE };
	}

	if (fiber->frame_count + 1 > fiber->frame_capacity) {
		uint new_capacity = fmin(LIT_CALL_FRAMES_MAX, fiber->frame_capacity * 2);
		fiber->frames = (LitCallFrame*) lit_reallocate(state, fiber->frames, sizeof(LitCallFrame) * fiber->frame_capacity, sizeof(LitCallFrame) * new_capacity);
		fiber->frame_capacity = new_capacity;
	}

	LitFunction* function;

	if (state->api_function == NULL) {
		function = state->api_function = lit_create_function(state, module);
		LitChunk* chunk = &function->chunk;

		chunk->has_line_info = false;
		function->name = state->api_name;

		lit_write_chunk(state, chunk, OP_CALL, 1);
		lit_write_chunk(state, chunk, 0, 1);
		lit_write_chunk(state, chunk, OP_RETURN, 1);
	}

	function = state->api_function;

	// Hot patch the argument count into the OP_CALL instruction
	function->chunk.code[1] = argument_count;
	function->max_slots = 3 + argument_count;

	lit_ensure_fiber_stack(state, fiber, function->max_slots + (int) (fiber->stack_top - fiber->stack));

	LitCallFrame* frame = &fiber->frames[fiber->frame_count++];
	frame->slots = fiber->stack_top;

	#define PUSH(value) (*fiber->stack_top++ = value)
	PUSH(OBJECT_VALUE(function));
	PUSH(callee);

	for (uint8_t i = 0; i < argument_count; i++) {
		PUSH(arguments[i]);
	}
	#undef PUSH

	frame->ip = function->chunk.code;
	frame->closure = NULL;
	frame->function = function;
	frame->result_ignored = false;

	LitFiber* previous = state->vm->fiber;
	LitInterpretResult result = lit_interpret_fiber(state, fiber);
	state->vm->fiber = previous;

	lit_trace_frame(state->vm->fiber);

	if (state->api_fiber == NULL) {
		state->api_fiber = fiber;
	}

	if (fiber->error != NULL_VALUE) {
		result.result = fiber->error;
		fiber->error = NULL_VALUE;
		fiber->abort = false;
		fiber->stack_top = fiber->stack;
		fiber->frame_count = 0;
	}

	return result;
}

LitInterpretResult lit_call_function(LitState* state, LitModule* module, LitFunction* callee, LitValue* arguments, uint8_t argument_count) {
	if (callee == NULL) {
		return (LitInterpretResult) { INTERPRET_INVALID, NULL_VALUE };
	}

	return lit_call(state, module, OBJECT_VALUE(callee), arguments, argument_count);
}

double lit_check_number(LitVm* vm, LitValue* args, uint8_t arg_count, uint8_t id) {
	if (arg_count <= id || !IS_NUMBER(args[id])) {
		lit_runtime_error_exiting(vm, "Expected a number as argument #%i, got a %s", (int) id, id >= arg_count ? "null" : lit_get_value_type(args[id]));
	}

	return AS_NUMBER(args[id]);
}

double lit_get_number(LitVm* vm, LitValue* args, uint8_t arg_count, uint8_t id, double def) {
	if (arg_count <= id || !IS_NUMBER(args[id])) {
		return def;
	}

	return AS_NUMBER(args[id]);
}

bool lit_check_bool(LitVm* vm, LitValue* args, uint8_t arg_count, uint8_t id) {
	if (arg_count <= id || !IS_BOOL(args[id])) {
		lit_runtime_error_exiting(vm, "Expected a boolean as argument #%i, got a %s", (int) id, id >= arg_count ? "null" : lit_get_value_type(args[id]));
	}

	return AS_BOOL(args[id]);
}

bool lit_get_bool(LitVm* vm, LitValue* args, uint8_t arg_count, uint8_t id, bool def) {
	if (arg_count <= id || !IS_BOOL(args[id])) {
		return def;
	}

	return AS_BOOL(args[id]);
}

const char* lit_check_string(LitVm* vm, LitValue* args, uint8_t arg_count, uint8_t id) {
	if (arg_count <= id || !IS_STRING(args[id])) {
		lit_runtime_error_exiting(vm, "Expected a string as argument #%i, got a %s", (int) id, id >= arg_count ? "null" : lit_get_value_type(args[id]));
	}

	return AS_STRING(args[id])->chars;
}

const char* lit_get_string(LitVm* vm, LitValue* args, uint8_t arg_count, uint8_t id, const char* def) {
	if (arg_count <= id || !IS_STRING(args[id])) {
		return def;
	}

	return AS_STRING(args[id])->chars;
}

LitString* lit_check_object_string(LitVm* vm, LitValue* args, uint8_t arg_count, uint8_t id) {
	if (arg_count <= id || !IS_STRING(args[id])) {
		lit_runtime_error_exiting(vm, "Expected a string as argument #%i, got a %s", (int) id, id >= arg_count ? "null" : lit_get_value_type(args[id]));
	}

	return AS_STRING(args[id]);
}

LitInstance* lit_check_instance(LitVm* vm, LitValue* args, uint8_t arg_count, uint8_t id) {
	if (arg_count <= id || !IS_INSTANCE(args[id])) {
		lit_runtime_error_exiting(vm, "Expected an instance as argument #%i, got a %s", (int) id, id >= arg_count ? "null" : lit_get_value_type(args[id]));
	}

	return AS_INSTANCE(args[id]);
}

LitValue* lit_check_reference(LitVm* vm, LitValue* args, uint8_t arg_count, uint8_t id) {
	if (arg_count <= id || !IS_REFERENCE(args[id])) {
		lit_runtime_error_exiting(vm, "Expected a reference as argument #%i, got a %s", (int) id, id >= arg_count ? "null" : lit_get_value_type(args[id]));
	}

	return AS_REFERENCE(args[id])->slot;
}

void lit_ensure_bool(LitVm* vm, LitValue value, const char* error){
	if (!IS_BOOL(value)) {
		lit_runtime_error_exiting(vm, error);
	}
}

void lit_ensure_string(LitVm* vm, LitValue value, const char* error) {
	if (!IS_STRING(value)) {
		lit_runtime_error_exiting(vm, error);
	}
}

void lit_ensure_number(LitVm* vm, LitValue value, const char* error) {
	if (!IS_NUMBER(value)) {
		lit_runtime_error_exiting(vm, error);
	}
}

void lit_ensure_object_type(LitVm* vm, LitValue value, LitObjectType type, const char* error) {
	if (!IS_OBJECT(value) || OBJECT_TYPE(value) != type) {
		lit_runtime_error_exiting(vm, error);
	}
}

LitValue lit_get_field(LitState* state, LitTable* table, const char* name) {
	LitValue value;

	if (!lit_table_get(table, CONST_STRING(state, name), &value)) {
		value = NULL_VALUE;
	}

	return value;
}

LitValue lit_get_map_field(LitState* state, LitMap* map, const char* name) {
	LitValue value;

	if (!lit_table_get(&map->values, CONST_STRING(state, name), &value)) {
		value = NULL_VALUE;
	}

	return value;
}

void lit_set_field(LitState* state, LitTable* table, const char* name, LitValue value) {
	lit_table_set(state, table, CONST_STRING(state, name), value);
}

void lit_set_map_field(LitState* state, LitMap* map, const char* name, LitValue value) {
	lit_table_set(state, &map->values, CONST_STRING(state, name), value);
}

LitString* lit_to_string(LitState* state, LitValue object) {
	if (IS_STRING(object)) {
		return AS_STRING(object);
	} else if (!IS_OBJECT(object)) {
		if (IS_NULL(object)) {
			return CONST_STRING(state, "null");
		} else if (IS_NUMBER(object)) {
			return AS_STRING(lit_number_to_string(state, AS_NUMBER(object)));
		} else if (IS_BOOL(object)) {
			return CONST_STRING(state, AS_BOOL(object) ? "true" : "false");
		}
	} else if (IS_REFERENCE(object)) {
		LitValue* slot = AS_REFERENCE(object)->slot;

		if (slot == NULL) {
			return CONST_STRING(state, "null");
		}

		return lit_to_string(state, *slot);
	}

	LitVm* vm = state->vm;
	LitFiber* fiber = state->api_fiber;

	if (fiber == NULL) {
		fiber = lit_create_fiber(state, state->vm->fiber->module, NULL);
		fiber->frame_count = 0;
	} else {
		// Make it busy
		state->api_fiber = NULL;
	}

	if (fiber->frame_count == LIT_CALL_FRAMES_MAX) {
		lit_runtime_error(vm, "Stack overflow");
		return CONST_STRING(state, "null");
	}

	if (fiber->frame_count + 1 > fiber->frame_capacity) {
		uint new_capacity = fmin(LIT_CALL_FRAMES_MAX, fiber->frame_capacity * 2);
		fiber->frames = (LitCallFrame*) lit_reallocate(state, fiber->frames, sizeof(LitCallFrame) * fiber->frame_capacity, sizeof(LitCallFrame) * new_capacity);
		fiber->frame_capacity = new_capacity;
	}

	LitFunction* function;

	if (state->api_string_function == NULL) {
		function = state->api_string_function = lit_create_function(state, fiber->module);
		LitChunk* chunk = &function->chunk;

		chunk->has_line_info = false;
		function->name = state->api_name;
		function->max_slots = 3;

		lit_write_chunk(state, chunk, OP_INVOKE, 1);
		lit_emit_short(state, chunk, lit_chunk_add_constant(state, chunk, OBJECT_CONST_STRING(state, "toString")));
		lit_emit_bytes(state, chunk, 0, OP_RETURN);
	}

	function = state->api_string_function;
	lit_ensure_fiber_stack(state, fiber, function->max_slots + (int) (fiber->stack_top - fiber->stack));

	LitCallFrame* frame = &fiber->frames[fiber->frame_count++];

	frame->ip = function->chunk.code;
	frame->closure = NULL;
	frame->function = function;
	frame->slots = fiber->stack_top;
	frame->result_ignored = false;

	#define PUSH(value) (*fiber->stack_top++ = value)
	PUSH(OBJECT_VALUE(function));
	PUSH(object);
	#undef PUSH

	LitFiber* previous = state->vm->fiber;
	LitInterpretResult result = lit_interpret_fiber(state, fiber);
	state->vm->fiber = previous;

	lit_trace_frame(state->vm->fiber);

	if (state->api_fiber == NULL) {
		state->api_fiber = fiber;
	}

	if (result.type != INTERPRET_OK) {
		return CONST_STRING(state, "null");
	}

	return AS_STRING(result.result);
}
