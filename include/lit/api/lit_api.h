#ifndef LIT_API_H
#define LIT_API_H

#include <lit/lit_predefines.h>
#include <lit/lit_common.h>
#include <lit/vm/lit_object.h>

#define LIT_NATIVE(name) static LitValue name##_native(LitVm* vm, uint arg_count, LitValue* args)

void lit_init_api(LitState* state);
void lit_free_api(LitState* state);

LitValue lit_get_global(LitState* state, LitString* name);
void lit_set_global(LitState* state, LitString* name, LitValue value);
bool lit_global_exists(LitState* state, LitString* name);
void lit_define_native(LitState* state, const char* name, LitNativeFn native);

LitInterpretResult lit_call(LitState* state, LitValue callee, LitValue* arguments, uint8_t argument_count);

#endif