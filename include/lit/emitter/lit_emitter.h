#ifndef LIT_EMITTER_H
#define LIT_EMITTER_H

#include <lit/lit_common.h>
#include <lit/lit_predefines.h>
#include <lit/vm/lit_chunk.h>
#include <lit/parser/lit_ast.h>

typedef struct {
	LitState* state;
	bool had_error;
} LitEmitter;

void lit_init_emitter(LitState* state, LitEmitter* emitter);
void lit_free_emitter(LitEmitter* emitter);

LitChunk* lit_emit(LitEmitter* emitter, LitStatements* statements);

#endif