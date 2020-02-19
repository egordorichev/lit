#ifndef LIT_API_H
#define LIT_API_H

#include <lit/lit_predefines.h>
#include <lit/lit_common.h>

void lit_init_api(LitState* state);
void lit_free_api(LitState* state);

LitValue lit_get_global(LitState* state, LitString* name);
void lit_set_global(LitState* state, LitString* name, LitValue value);
bool lit_global_exists(LitState* state, LitString* name);

LitInterpretResult lit_call(LitState* state, LitValue callee, LitValue* arguments, uint8_t argument_count);

#endif