#ifndef LIT_API_H
#define LIT_API_H

#include "lit/lit_predefines.h"
#include "lit/lit_common.h"
#include "lit/vm/lit_object.h"
#include "lit/api/lit_calls.h"

#include <string.h>

#define RETURN_RUNTIME_ERROR() return (LitInterpretResult) { INTERPRET_RUNTIME_ERROR, NULL_VALUE };
#define RETURN_OK(r) return (LitInterpretResult) { INTERPRET_OK, r };

#define LIT_NATIVE(name) static LitValue name##_native(LitVm* vm, uint arg_count, LitValue* args)
#define LIT_METHOD(name) static LitValue name(LitVm* vm, LitValue instance, uint arg_count, LitValue* args)
#define LIT_PRIMITIVE(name) static bool name(LitVm* vm, LitValue instance, uint arg_count, LitValue* args)
#define LIT_NATIVE_PRIMITIVE(name) static bool name##_primitive(LitVm* vm, uint arg_count, LitValue* args)

#define LIT_BEGIN_CLASS(name) { \
  bool was_allowed = state->allow_gc; \
  state->allow_gc = false; \
	LitString* klass_name = lit_copy_string(state, name, strlen(name)); \
	LitClass* klass = lit_create_class(state, klass_name);

#define LIT_INHERIT_CLASS(super_klass) klass->super = (LitClass*) super_klass; \
	if (klass->init_method == NULL) { \
    klass->init_method = super_klass->init_method; \
  } \
  lit_table_add_all_ignoring(state, &super_klass->methods, &klass->methods); \
	lit_table_add_all_ignoring(state, &super_klass->static_fields, &klass->static_fields);

#define LIT_END_CLASS_IGNORING() lit_set_global(state, klass_name, OBJECT_VALUE(klass)); \
    state->allow_gc = was_allowed; \
	}

#define LIT_END_CLASS() lit_set_global(state, klass_name, OBJECT_VALUE(klass)); \
    if (klass->super == NULL) { \
			LIT_INHERIT_CLASS(state->object_class); \
    } \
    state->allow_gc = was_allowed; \
	}

#define LIT_BIND_METHOD(name, method) { LitString* nm = lit_copy_string(state, name, strlen(name)); lit_table_set(state, &klass->methods, nm, OBJECT_VALUE(lit_create_native_method(state, method, nm))); }
#define LIT_BIND_PRIMITIVE(name, method) { LitString* nm = lit_copy_string(state, name, strlen(name)); lit_table_set(state, &klass->methods, nm, OBJECT_VALUE(lit_create_primitive_method(state, method, nm))); }
#define LIT_BIND_CONSTRUCTOR(method) { LitString* nm = lit_copy_string(state, "constructor", 11); LitNativeMethod* m = lit_create_native_method(state, method, nm); klass->init_method = (LitObject*) m; lit_table_set(state, &klass->methods, nm, OBJECT_VALUE(m)); }
#define LIT_BIND_STATIC_METHOD(name, method) { LitString* nm = lit_copy_string(state, name, strlen(name)); lit_table_set(state, &klass->static_fields, nm, OBJECT_VALUE(lit_create_native_method(state, method, nm))); }
#define LIT_BIND_STATIC_PRIMITIVE(name, method) { LitString* nm = lit_copy_string(state, name, strlen(name)); lit_table_set(state, &klass->static_fields, nm, OBJECT_VALUE(lit_create_primitive_method(state, method, nm))); }
#define LIT_SET_STATIC_FIELD(name, field) { LitString* nm = lit_copy_string(state, name, strlen(name)); lit_table_set(state, &klass->static_fields, nm, field); }

#define LIT_BIND_SETTER(name, setter) { LitString* nm = lit_copy_string(state, name, strlen(name)); lit_table_set(state, &klass->methods, nm, OBJECT_VALUE(lit_create_field(state, NULL, (LitObject*) lit_create_native_method(state, setter, nm)))); }
#define LIT_BIND_GETTER(name, getter) { LitString* nm = lit_copy_string(state, name, strlen(name)); lit_table_set(state, &klass->methods, nm, OBJECT_VALUE(lit_create_field(state, (LitObject*) lit_create_native_method(state, getter, nm), NULL))); }
#define LIT_BIND_FIELD(name, getter, setter) { LitString* nm = lit_copy_string(state, name, strlen(name)); lit_table_set(state, &klass->methods, nm, OBJECT_VALUE(lit_create_field(state, (LitObject*) lit_create_native_method(state, getter, nm), (LitObject*) lit_create_native_method(state, setter, nm)))); }

#define LIT_BIND_STATIC_SETTER(name, setter) { LitString* nm = lit_copy_string(state, name, strlen(name)); lit_table_set(state, &klass->static_fields, nm, OBJECT_VALUE(lit_create_field(state, NULL, (LitObject*) lit_create_native_method(state, setter, nm)))); }
#define LIT_BIND_STATIC_GETTER(name, getter) { LitString* nm = lit_copy_string(state, name, strlen(name)); lit_table_set(state, &klass->static_fields, nm, OBJECT_VALUE(lit_create_field(state, (LitObject*) lit_create_native_method(state, getter, nm), NULL))); }
#define LIT_BIND_STATIC_FIELD(name, getter, setter) { LitString* nm = lit_copy_string(state, name, strlen(name)); lit_table_set(state, &klass->static_fields, nm, OBJECT_VALUE(lit_create_field(state, (LitObject*) lit_create_native_method(state, getter, nm), (LitObject*) lit_create_native_method(state, setter, nm)))); }

void lit_init_api(LitState* state);
void lit_free_api(LitState* state);

LitValue lit_get_global(LitState* state, LitString* name);
LitFunction* lit_get_global_function(LitState* state, LitString* name);

void lit_set_global(LitState* state, LitString* name, LitValue value);
bool lit_global_exists(LitState* state, LitString* name);
void lit_define_native(LitState* state, const char* name, LitNativeFunctionFn native);
void lit_define_native_primitive(LitState* state, const char* name, LitNativePrimitiveFn native);

#define LIT_CHECK_NUMBER(id) lit_check_number(vm, args, arg_count, id)
#define LIT_GET_NUMBER(id, def) lit_get_number(vm, args, arg_count, id, def)

#define LIT_CHECK_BOOL(id) lit_check_bool(vm, args, arg_count, id)
#define LIT_GET_BOOL(id, def) lit_get_bool(vm, args, arg_count, id, def)

#define LIT_CHECK_STRING(id) lit_check_string(vm, args, arg_count, id)
#define LIT_GET_STRING(id, def) lit_get_string(vm, args, arg_count, id, def)

#define LIT_CHECK_OBJECT_STRING(id) lit_check_object_string(vm, args, arg_count, id)
#define LIT_CHECK_INSTANCE(id) lit_check_instance(vm, args, arg_count, id)
#define LIT_CHECK_REFERENCE(id) lit_check_reference(vm, args, arg_count, id)

double lit_check_number(LitVm* vm, LitValue* args, uint8_t arg_count, uint8_t id);
double lit_get_number(LitVm* vm, LitValue* args, uint8_t arg_count, uint8_t id, double def);

bool lit_check_bool(LitVm* vm, LitValue* args, uint8_t arg_count, uint8_t id);
bool lit_get_bool(LitVm* vm, LitValue* args, uint8_t arg_count, uint8_t id, bool def);

const char* lit_check_string(LitVm* vm, LitValue* args, uint8_t arg_count, uint8_t id);
const char* lit_get_string(LitVm* vm, LitValue* args, uint8_t arg_count, uint8_t id, const char* def);

LitString* lit_check_object_string(LitVm* vm, LitValue* args, uint8_t arg_count, uint8_t id);
LitInstance* lit_check_instance(LitVm* vm, LitValue* args, uint8_t arg_count, uint8_t id);
LitValue* lit_check_reference(LitVm* vm, LitValue* args, uint8_t arg_count, uint8_t id);

void lit_ensure_bool(LitVm* vm, LitValue value, const char* error);
void lit_ensure_string(LitVm* vm, LitValue value, const char* error);
void lit_ensure_number(LitVm* vm, LitValue value, const char* error);
void lit_ensure_object_type(LitVm* vm, LitValue value, LitObjectType type, const char* error);

#define LIT_GET_FIELD(id) lit_get_field(vm->state, &AS_INSTANCE(instance)->fields, id)
#define LIT_GET_MAP_FIELD(id) lit_get_map_field(vm->state, &AS_INSTANCE(instance)->fields, id)
#define LIT_SET_FIELD(id, value) lit_set_field(vm->state, &AS_INSTANCE(instance)->fields, id, value)
#define LIT_SET_MAP_FIELD(id, value) lit_set_map_field(vm->state, &AS_INSTANCE(instance)->fields, id, value)

LitValue lit_get_field(LitState* state, LitTable* table, const char* name);
LitValue lit_get_map_field(LitState* state, LitMap* map, const char* name);

void lit_set_field(LitState* state, LitTable* table, const char* name, LitValue value);
void lit_set_map_field(LitState* state, LitMap* map, const char* name, LitValue value);

#define LIT_ENSURE_ARGS(count) \
	if (arg_count != count) { \
		lit_runtime_error_exiting(vm, "Expected %i argument, got %i", count, arg_count); \
		return NULL_VALUE; \
	}

#define LIT_ENSURE_MIN_ARGS(count) \
	if (arg_count < count) { \
		lit_runtime_error_exiting(vm, "Expected minimum %i argument, got %i", count, arg_count); \
		return NULL_VALUE; \
	}

#define LIT_ENSURE_MAX_ARGS(count) \
	if (arg_count > count) { \
		lit_runtime_error_exiting(vm, "Expected maximum %i argument, got %i", count, arg_count); \
		return NULL_VALUE; \
	}

#define LIT_INSERT_DATA(type, cleanup) ({\
	  LitUserdata* userdata = lit_create_userdata(vm->state, sizeof(type));\
	  userdata->cleanup_fn = cleanup;\
	  lit_table_set(vm->state, &AS_INSTANCE(instance)->fields, CONST_STRING(vm->state, "_data"), OBJECT_VALUE(userdata)); \
	  (type*) userdata->data;\
  })

#define LIT_EXTRACT_DATA(type) ({ \
    LitValue _d; \
		if (!lit_table_get(&AS_INSTANCE(instance)->fields, CONST_STRING(vm->state, "_data"), &_d)) { \
			lit_runtime_error_exiting(vm, "Failed to extract userdata");\
		} \
		(type*) AS_USERDATA(_d)->data; \
	})

#define LIT_EXTRACT_DATA_FROM(from, type) ({ \
    LitValue _d; \
		if (!lit_table_get(&AS_INSTANCE(from)->fields, CONST_STRING(vm->state, "_data"), &_d)) { \
			lit_runtime_error_exiting(vm, "Failed to extract userdata");\
		} \
		(type*) AS_USERDATA(_d)->data; \
	})

#endif