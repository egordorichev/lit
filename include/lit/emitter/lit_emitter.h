#ifndef LIT_EMITTER_H
#define LIT_EMITTER_H

#include <lit/lit_common.h>
#include <lit/lit_predefines.h>
#include <lit/vm/lit_chunk.h>
#include <lit/parser/lit_ast.h>
#include <lit/util/lit_array.h>
#include <lit/vm/lit_object.h>

typedef struct {
	const char* name;
	uint length;
	int depth;
} LitLocal;

DECLARE_ARRAY(LitLocals, LitLocal, locals)

typedef struct {
	LitLocals locals;

	int scope_depth;

	LitFunction* function;
	LitFunctionType type;
	struct LitCompiler* enclosing;

	bool skip_return;
} LitCompiler;

typedef struct sLitEmitter {
	LitState* state;
	LitChunk* chunk;
	LitCompiler* compiler;

	uint last_line;
	uint loop_start;

	LitUInts breaks;
} sLitEmitter;

void lit_init_emitter(LitState* state, LitEmitter* emitter);
void lit_free_emitter(LitEmitter* emitter);

LitFunction* lit_emit(LitEmitter* emitter, LitStatements* statements);

#endif