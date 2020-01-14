#ifndef LIT_STATE_H
#define LIT_STATE_H

#include <lit/lit_common.h>
#include <lit/lit_predefines.h>
#include <lit/scanner/lit_scanner.h>

typedef struct {
	uint bytes_allocated;

	struct sLitVm* vm;
	LitScanner scanner;
} LitState;

LitState* lit_new_state();
void lit_free_state(LitState* state);

#endif