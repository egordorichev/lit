#ifndef LIT_CALLS_H
#define LIT_CALLS_H

#include "lit_predefines.h"
#include "lit_common.h"
#include "vm/lit_object.h"
#include "vm/lit_vm.h"

LitInterpretResult lit_call_function(LitState* state, LitFunction* callee, LitValue* arguments, uint8_t argument_count);
LitInterpretResult lit_call_method(LitState* state, LitValue instance, LitValue callee, LitValue* arguments, uint8_t argument_count);
LitInterpretResult lit_call(LitState* state, LitValue callee, LitValue* arguments, uint8_t argument_count);
LitInterpretResult lit_find_and_call_method(LitState* state, LitValue callee, LitString* method_name, LitValue* arguments, uint8_t argument_count);

LitString* lit_to_string(LitState* state, LitValue object);
LitValue lit_call_new(LitVm* vm, const char* name, LitValue* args, uint arg_count);

#endif