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

#endif