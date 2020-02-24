#include <lit/api/lit_api.h>
#include <lit/vm/lit_vm.h>
#include <lit/vm/lit_chunk.h>
#include <lit/vm/lit_object.h>

#include <string.h>

void lit_init_api(LitState* state) {
	state->api_module = lit_create_module(state, lit_copy_string(state, "%c%", 3));

	state->api_function = lit_create_function(state);
	state->api_function->name = lit_copy_string(state, "%c api%", 7);

	state->api_fiber = lit_create_fiber(state, state->api_module, state->api_function);
}

void lit_free_api(LitState* state) {
	state->api_fiber = NULL;
	state->api_function = NULL;
	state->api_fiber = NULL;
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
	lit_push_root(state, (LitObject *) name);
	lit_push_value_root(state, value);
	lit_table_set(state, &state->vm->globals, name, value);
	lit_pop_roots(state, 2);
}

bool lit_global_exists(LitState* state, LitString* name) {
	LitValue global;
	return lit_table_get(&state->vm->globals, name, &global);
}

void lit_define_native(LitState* state, const char* name, LitNativeFunctionFn native) {
	lit_push_root(state, (LitObject *) lit_create_native_function(state, native));
	lit_push_root(state, (LitObject *) CONST_STRING(state, name));
	lit_table_set(state, &state->vm->globals, AS_STRING(lit_peek_root(state, 0)), lit_peek_root(state, 1));
	lit_pop_roots(state, 2);
}

LitInterpretResult lit_call(LitState* state, LitValue callee, LitValue* arguments, uint8_t argument_count) {
	LitFiber* fiber = state->api_fiber;
	LitFunction* function = fiber->frames[0].function;
	LitChunk* chunk = &function->chunk;

	fiber->frame_count = 1;

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

	LitInterpretResult result = lit_interpret_fiber(state, fiber);
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
		lit_runtime_error(vm, "Expected a number as argument #%g", id);
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
		lit_runtime_error(vm, "Expected a boolean as argument #%g", id);
		return 0;
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
		lit_runtime_error(vm, "Expected a string as argument #%g", id);
		return 0;
	}

	return AS_STRING(args[id])->chars;
}

const char* lit_get_string(LitVm* vm, LitValue* args, uint8_t arg_count, uint8_t id, const char* def) {
	if (arg_count <= id || !IS_STRING(args[id])) {
		return def;
	}

	return AS_STRING(args[id])->chars;
}