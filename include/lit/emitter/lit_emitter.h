#ifndef LIT_EMITTER_H
#define LIT_EMITTER_H

#include <lit/lit_common.h>
#include <lit/lit_predefines.h>
#include <lit/vm/lit_chunk.h>
#include <lit/parser/lit_ast.h>

typedef struct {
	const char* name;
	uint length;
	uint16_t depth;
} LitLocal;

typedef struct {
	LitLocal locals[UINT8_MAX + 1];
	uint16_t local_count;
	uint16_t scope_depth;
} LitCompiler;

typedef struct sLitEmitter {
	LitState* state;
	LitChunk* chunk;
	LitCompiler* compiler;
} sLitEmitter;

void lit_init_emitter(LitState* state, LitEmitter* emitter);
void lit_free_emitter(LitEmitter* emitter);

LitChunk* lit_emit(LitEmitter* emitter, LitStatements* statements);

#endif