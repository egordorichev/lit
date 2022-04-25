#include "lit/std/lit_time.h"
#include "lit/api/lit_api.h"
#include "lit/state/lit_state.h"

LIT_METHOD(timer_add) {
	LIT_ENSURE_ARGS(2)
	LitValue callback = args[0];

	if (!IS_CALLABLE_FUNCTION(callback)) {
		lit_runtime_error_exiting(vm, "Expected a function as the callback");
	}

	uint64_t delay = LIT_CHECK_NUMBER(1);
	lit_register_event(vm->state, callback, delay);

	return NULL_VALUE;
}

void lit_open_event_library(LitState* state) {
	LIT_BEGIN_CLASS("Timer")
		LIT_BIND_STATIC_METHOD("add", timer_add)
	LIT_END_CLASS()
}