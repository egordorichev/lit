#include "lit/std/lit_gc.h"
#include "lit/api/lit_api.h"
#include "lit/state/lit_state.h"

LIT_METHOD(gc_memory_used) {
	return NUMBER_VALUE(vm->state->bytes_allocated);
}

LIT_METHOD(gc_next_round) {
	return NUMBER_VALUE(vm->state->next_gc);
}

LIT_METHOD(gc_trigger) {
	vm->state->allow_gc = true;
	int64_t collected = lit_collect_garbage(vm);
	vm->state->allow_gc = false;

	return NUMBER_VALUE(collected);
}

void lit_open_gc_library(LitState* state) {
	LIT_BEGIN_CLASS("GC")
		LIT_BIND_STATIC_GETTER("memoryUsed", gc_memory_used)
		LIT_BIND_STATIC_GETTER("nextRound", gc_next_round)

		LIT_BIND_STATIC_METHOD("trigger", gc_trigger)
	LIT_END_CLASS()
}