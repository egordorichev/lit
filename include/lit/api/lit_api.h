#ifndef LIT_API_H
#define LIT_API_H

#include <lit/lit_predefines.h>
#include <lit/lit_common.h>
#include <lit/vm/lit_object.h>

#include <string.h>

#define LIT_NATIVE(name) static LitValue name##_native(LitVm* vm, uint arg_count, LitValue* args)
#define LIT_METHOD(name) static LitValue name(LitVm* vm, LitValue instance, uint arg_count, LitValue* args)
#define LIT_BEGIN_CLASS(name) { \
	LitString* klass_name = lit_copy_string(state, name, strlen(name)); \
	LitClass* klass = lit_create_class(state, klass_name);

#define LIT_INHERIT_CLASS(super_klass) klass->super = (LitClass *) super_klass; \
	klass->init_method = super_klass->init_method; \
	lit_table_add_all(state, &super_klass->methods, &klass->methods);

#define LIT_END_CLASS() lit_set_global(state, klass_name, OBJECT_VALUE(klass)); \
	}

#define LIT_BIND_METHOD(name, method) lit_table_set(state, &klass->methods, lit_copy_string(state, name, strlen(name)), OBJECT_VALUE(lit_create_native_method(state, method)));
#define LIT_BIND_STATIC_METHOD(name, method) lit_table_set(state, &klass->static_fields, lit_copy_string(state, name, strlen(name)), OBJECT_VALUE(lit_create_native_method(state, method)));
#define LIT_BIND_STATIC_FIELD(name, field) lit_table_set(state, &klass->static_fields, lit_copy_string(state, name, strlen(name)), field);

void lit_init_api(LitState* state);
void lit_free_api(LitState* state);

LitValue lit_get_global(LitState* state, LitString* name);
LitFunction* lit_get_global_function(LitState* state, LitString* name);

void lit_set_global(LitState* state, LitString* name, LitValue value);
bool lit_global_exists(LitState* state, LitString* name);
void lit_define_native(LitState* state, const char* name, LitNativeFunctionFn native);

LitInterpretResult lit_call(LitState* state, LitValue callee, LitValue* arguments, uint8_t argument_count);
LitInterpretResult lit_call_function(LitState* state, LitFunction* callee, LitValue* arguments, uint8_t argument_count);

#define LIT_CHECK_NUMBER(id) lit_check_number(vm, args, arg_count, id)
#define LIT_GET_NUMBER(id, def) lit_get_number(vm, args, arg_count, id, def)

#define LIT_CHECK_BOOL(id) lit_check_bool(vm, args, arg_count, id)
#define LIT_GET_BOOL(id, def) lit_get_bool(vm, args, arg_count, id, def)

#define LIT_CHECK_STRING(id) lit_check_string(vm, args, arg_count, id)
#define LIT_GET_STRING(id, def) lit_get_string(vm, args, arg_count, id, def)

double lit_check_number(LitVm* vm, LitValue* args, uint8_t arg_count, uint8_t id);
double lit_get_number(LitVm* vm, LitValue* args, uint8_t arg_count, uint8_t id, double def);

bool lit_check_bool(LitVm* vm, LitValue* args, uint8_t arg_count, uint8_t id);
bool lit_get_bool(LitVm* vm, LitValue* args, uint8_t arg_count, uint8_t id, bool def);

const char* lit_check_string(LitVm* vm, LitValue* args, uint8_t arg_count, uint8_t id);
const char* lit_get_string(LitVm* vm, LitValue* args, uint8_t arg_count, uint8_t id, const char* def);

LitValue* lit_get_field(LitState* state, LitMap* map, const char* name);

#endif