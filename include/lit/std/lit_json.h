#ifndef LIT_JSON_H
#define LIT_JSON_H

#include "lit_predefines.h"

LitValue lit_json_parse(LitVm* vm, LitString* string);
LitString* lit_json_to_string(LitVm* vm, LitValue instance, uint indentation);
void lit_open_json_library(LitState* state);

#endif