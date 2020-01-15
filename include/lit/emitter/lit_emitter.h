#ifndef LIT_EMITTER_H
#define LIT_EMITTER_H

#include <lit/lit_common.h>
#include <lit/lit_predefines.h>
#include <lit/vm/lit_chunk.h>
#include <lit/parser/lit_ast.h>

typedef struct sLitEmitter {
	LitState* state;
	LitChunk* chunk;
	bool had_error;
} sLitEmitter;

void lit_init_emitter(LitState* state, LitEmitter* emitter);
void lit_free_emitter(LitEmitter* emitter);

LitChunk* lit_emit(LitEmitter* emitter, LitStatements* statements);

#endif