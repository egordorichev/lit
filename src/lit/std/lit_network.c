#include "std/lit_network.h"
#include "api/lit_api.h"
#include "state/lit_state.h"

LIT_METHOD(network_post) {

	return NULL_VALUE;
}

void lit_open_network_library(LitState* state) {
	LIT_BEGIN_CLASS("Network")
		LIT_BIND_STATIC_METHOD("post", network_post)
	LIT_END_CLASS()
}