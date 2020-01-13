#ifndef LIT_DEBUG_H
#define LIT_DEBUG_H

#include <lit/vm/lit_chunk.h>

void lit_disassemble_chunk(LitChunk* chunk, const char* name);
uint lit_disassemble_instruction(LitChunk* chunk, uint offset);

#endif