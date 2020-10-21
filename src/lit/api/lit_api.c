#include "api/lit_api.h"
#include "vm/lit_vm.h"
#include "vm/lit_chunk.h"
#include "vm/lit_object.h"
#include"debug/lit_debug.h"

#include <string.h>
#include <math.h>

void lit_init_api(LitState* state) {
	state->api_name = lit_copy_string(state, "c", 1);
	state->api_function = NULL;
	state->api_fiber = NULL;
}

void lit_free_api(LitState* state) {
	state->api_name = NULL;
	state->api_function = NULL;
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