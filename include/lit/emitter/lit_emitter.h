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
	bool finished_declaration;
} LitPrivate;

DECLARE_ARRAY(LitPrivates, LitPrivate, privates)

typedef struct {
	const char* name;
	uint length;
	int depth;
	bool captured;
} LitLocal;

DECLARE_ARRAY(LitLocals, LitLocal, locals)

typedef struct {
	uint8_t index;
	bool isLocal;
} LitCompilerUpvalue;

typedef struct {
	LitLocals locals;
	int scope_depth;

	LitFunction* function;
	LitFunctionType type;

	LitCompilerUpvalue upvalues[UINT8_COUNT];

	struct LitCompiler* enclosing;

	bool skip_return;
	uint loop_depth;
} LitCompiler;

typedef struct sLitEmitter {
	LitState* state;
	LitChunk* chunk;
	LitCompiler* compiler;

	uint last_line;
	uint loop_start;

	LitPrivates privates;
	LitUInts breaks;

	LitModule* module;
	LitString* class_name;

	bool class_has_super;
	bool previous_was_expression_statement;
	bool emit_pop_continue;
} sLitEmitter;

void lit_init_emitter(LitState* state, LitEmitter* emitter);
void lit_free_emitter(LitEmitter* emitter);

LitModule* lit_emit(LitEmitter* emitter, LitStatements* statements, LitString* module_name);

#endif