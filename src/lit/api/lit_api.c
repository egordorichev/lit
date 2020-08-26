#include <lit/api/lit_api.h>
#include <lit/vm/lit_vm.h>
#include <lit/vm/lit_chunk.h>
#include <lit/vm/lit_object.h>

#include <string.h>
#include <lit/debug/lit_debug.h>

static uint emit_constant(LitState* state, LitChunk* chunk, LitValue value) {
	uint constant = lit_chunk_add_constant(state, chunk, value);

	if (constant < UINT8_MAX) {
		lit_emit_bytes(state, chunk, OP_CONSTANT, constant);
	} else if (constant < UINT16_MAX) {
		lit_emit_byte(state, chunk, OP_CONSTANT_LONG);
		lit_emit_short(state, chunk, constant);
	} else {
		lit_runtime_error(state->vm, "Too many constants in one chunk");
	}

	return constant;
}

void lit_init_api(LitState* state) {
	state->api_name = lit_copy_string(state, "c", 1);
	state->api_module = lit_create_module(state, state->api_name);
}

void lit_free_api(LitState* state) {
	state->api_module = NULL;
}

LitValue lit_get_global(LitState* state, LitString* name) {
	LitValue global;

	if (!lit_table_get(&state->vm->globals, name, &global)) {
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
	lit_table_set(state, &state->vm->globals, name, value);
	lit_pop_roots(state, 2);
}

bool lit_global_exists(LitState* state, LitString* name) {
	LitValue global;
	return lit_table_get(&state->vm->globals, name, &global);
}

void lit_define_native(LitState* state, const char* name, LitNativeFunctionFn native) {
	lit_push_root(state, (LitObject*) CONST_STRING(state, name));
	lit_push_root(state, (LitObject*) lit_create_native_function(state, native, AS_STRING(lit_peek_root(state, 0))));
	lit_table_set(state, &state->vm->globals, AS_STRING(lit_peek_root(state, 1)), lit_peek_root(state, 0));
	lit_pop_roots(state, 2);
}

void lit_define_native_primitive(LitState* state, const char* name, LitNativePrimitiveFn native) {
	lit_push_root(state, (LitObject*) CONST_STRING(state, name));
	lit_push_root(state, (LitObject*) lit_create_native_primitive(state, native, AS_STRING(lit_peek_root(state, 0))));
	lit_table_set(state, &state->vm->globals, AS_STRING(lit_peek_root(state, 1)), lit_peek_root(state, 0));
	lit_pop_roots(state, 2);
}

LitInterpretResult lit_call(LitState* state, LitValue callee, LitValue* arguments, uint8_t argument_count) {
	LitFunction* function = lit_create_function(state, state->api_module);
	function->name = state->api_name;

	LitFiber* fiber = lit_create_fiber(state, state->api_module, function);
	LitChunk* chunk = &function->chunk;
	chunk->has_line_info = false;

#define PUSH(value) (*fiber->stack_top++ = value)

	PUSH(OBJECT_VALUE(function));
	PUSH(callee);

	for (uint8_t i = 0; i < argument_count; i++) {
		PUSH(arguments[i]);
	}

	lit_write_chunk(state, chunk, OP_CALL, 1);
	lit_write_chunk(state, chunk, argument_count, 1);
	lit_write_chunk(state, chunk, OP_RETURN, 1);

#undef PUSH

	function->max_slots = 2 + argument_count;
	fiber->frames[0].ip = chunk->code;

	LitInterpretResult result = lit_interpret_fiber(state, fiber);
	state->vm->fiber = fiber->parent;

	lit_free_chunk(state, chunk);
	return result;
}

LitInterpretResult lit_call_function(LitState* state, LitFunction* callee, LitValue* arguments, uint8_t argument_count) {
	if (callee == NULL) {
		return (LitInterpretResult) {};
	}

	return lit_call(state, OBJECT_VALUE(callee), arguments, argument_count);
}

double lit_check_number(LitVm* vm, LitValue* args, uint8_t arg_count, uint8_t id) {
	if (arg_count <= id || !IS_NUMBER(args[id])) {
		lit_runtime_error(vm, "Expected a number as argument #%x", id);
		return 0;
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
		lit_runtime_error(vm, "Expected a boolean as argument #%x", id);
		return false;
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
		lit_runtime_error(vm, "Expected a string as argument #%x", id);
		return NULL;
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
		lit_runtime_error(vm, "Expected a string as argument #%x", id);
		return NULL;
	}

	return AS_STRING(args[id]);
}

LitInstance* lit_check_instance(LitVm* vm, LitValue* args, uint8_t arg_count, uint8_t id) {
	if (arg_count <= id || !IS_INSTANCE(args[id])) {
		lit_runtime_error(vm, "Expected an instance as argument #%x", id);
		return NULL;
	}

	return AS_INSTANCE(args[id]);
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
	if (!IS_OBJECT(object)) {
		if (IS_NULL(object)) {
			return CONST_STRING(state, "null");
		} else if (IS_NUMBER(object)) {
			return AS_STRING(lit_number_to_string(state, AS_NUMBER(object)));
		} else if (IS_BOOL(object)) {
			return CONST_STRING(state, AS_BOOL(object) ? "true" : "false");
		}
	} else if (IS_STRING(object)) {
		return AS_STRING(object);
	}

	LitFunction* function = lit_create_function(state, state->api_module);
	function->name = state->api_name;

	LitFiber* fiber = lit_create_fiber(state, state->api_module, function);
	LitChunk* chunk = &function->chunk;

	chunk->has_line_info = false;

#define PUSH(value) (*fiber->stack_top++ = value)

	PUSH(OBJECT_VALUE(function));
	PUSH(object);

	lit_emit_byte(state, chunk, OP_INVOKE);
	lit_emit_short(state, chunk, lit_chunk_add_constant(state, chunk, OBJECT_CONST_STRING(state, "toString")));
	lit_emit_bytes(state, chunk, 0, OP_RETURN);

#undef PUSH

	function->max_slots = 2 + function->arg_count;
	fiber->frames[0].ip = chunk->code;

	LitFiber* last_fiber = state->vm->fiber;
	LitInterpretResult result = lit_interpret_fiber(state, fiber);

	state->vm->fiber = last_fiber;
	lit_free_chunk(state, chunk);

	if (!IS_STRING(result.result)) {
		return CONST_STRING(state, "null");
	}

	return AS_STRING(result.result);
}
