#ifndef LIT_OPTIMISER_H
#define LIT_OPTIMISER_H

#include <lit/lit_common.h>
#include <lit/lit_predefines.h>
#include <lit/parser/lit_ast.h>

typedef struct sLitOptimizer {
	LitState* state;
} sLitOptimizer;

void lit_init_optimizer(LitState* state, LitOptimizer* optimizer);
void lit_optimize(LitOptimizer* optimizer, LitStatements* statements);

typedef enum {
	OPTIMIZATION_CONSTANT_FOLDING,

	OPTIMIZATION_TOTAL
} LitOptimization;

bool lit_is_optimization_enabled(LitOptimization optimization);
void lit_set_optimization_enabled(LitOptimization optimization, bool enabled);

const char* lit_get_optimization_name(LitOptimization optimization);
const char* lit_get_optimization_description(LitOptimization optimization);

#endif