#ifndef LIT_OPTIMISER_H
#define LIT_OPTIMISER_H

#include "lit/lit_common.h"
#include "lit/lit_predefines.h"
#include "lit/parser/lit_ast.h"

typedef struct {
	const char* name;
	uint length;
	int depth;

	bool constant;
	bool used;

	LitValue constant_value;
	LitStatement** declaration;
} LitVariable;

DECLARE_ARRAY(LitVariables, LitVariable, variables)

typedef struct sLitOptimizer {
	LitState* state;

	LitVariables variables;
	int depth;
	bool mark_used;
} sLitOptimizer;

void lit_init_optimizer(LitState* state, LitOptimizer* optimizer);
void lit_optimize(LitOptimizer* optimizer, LitStatements* statements);

typedef enum {
	OPTIMIZATION_LEVEL_NONE,
	OPTIMIZATION_LEVEL_REPL,
	OPTIMIZATION_LEVEL_DEBUG,
	OPTIMIZATION_LEVEL_RELEASE,
	OPTIMIZATION_LEVEL_EXTREME,

	OPTIMIZATION_LEVEL_TOTAL
} LitOptimizationLevel;

const char* lit_get_optimization_level_description(LitOptimizationLevel level);

typedef enum {
	OPTIMIZATION_CONSTANT_FOLDING,
	OPTIMIZATION_LITERAL_FOLDING,
	OPTIMIZATION_UNUSED_VAR,
	OPTIMIZATION_UNREACHABLE_CODE,
	OPTIMIZATION_EMPTY_BODY,
	OPTIMIZATION_LINE_INFO,
	OPTIMIZATION_PRIVATE_NAMES,
	OPTIMIZATION_C_FOR,

	OPTIMIZATION_TOTAL
} LitOptimization;

bool lit_is_optimization_enabled(LitOptimization optimization);
void lit_set_optimization_enabled(LitOptimization optimization, bool enabled);
void lit_set_all_optimization_enabled(bool enabled);
void lit_set_optimization_level(LitOptimizationLevel level);

const char* lit_get_optimization_name(LitOptimization optimization);
const char* lit_get_optimization_description(LitOptimization optimization);

#endif