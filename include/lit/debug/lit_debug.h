#ifndef LIT_DEBUG_H
#define LIT_DEBUG_H

#include <lit/vm/lit_chunk.h>

void lit_disassemble_module(LitModule* module, const char* source);
void lit_disassemble_chunk(LitChunk* chunk, const char* name, const char* source);
uint lit_disassemble_instruction(LitChunk* chunk, uint offset, const char* source);

void lit_trace_frame(LitFiber* fiber);

#endif