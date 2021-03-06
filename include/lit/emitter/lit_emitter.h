#ifndef LIT_EMITTER_H
#define LIT_EMITTER_H

#include "lit_common.h"
#include "lit_predefines.h"
#include "vm/lit_chunk.h"
#include "parser/lit_ast.h"
#include "util/lit_array.h"
#include "vm/lit_object.h"

typedef struct {
	bool initialized;
	bool constant;
} LitPrivate;

DECLARE_ARRAY(LitPrivates, LitPrivate, privates)

typedef struct {
	const char* name;
	uint length;
	int depth;

	bool captured;
	bool constant;
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

	int slots;
	int max_slots;
} LitCompiler;

typedef struct sLitEmitter {
	LitState* state;
	LitChunk* chunk;
	LitCompiler* compiler;

	uint last_line;
	uint loop_start;

	LitPrivates privates;
	LitUInts breaks;
	LitUInts continues;

	LitModule* module;
	LitString* class_name;

	bool class_has_super;
	bool previous_was_expression_statement;
	int emit_reference;
} sLitEmitter;

void lit_init_emitter(LitState* state, LitEmitter* emitter);
void lit_free_emitter(LitEmitter* emitter);

LitModule* lit_emit(LitEmitter* emitter, LitStatements* statements, LitString* module_name);

#endif