#ifndef LIT_EVENT_H
#define LIT_EVENT_H

#include "lit/lit_predefines.h"

typedef struct LitEvent {
	uint64_t expire_time;
	LitValue callback;

	struct LitEvent* next;
	struct LitEvent* previous;
} LitEvent;

typedef struct LitEventSystem {
	LitEvent* events;
	LitEvent* last_event;
} LitEventSystem;

void lit_init_event_system(LitState* state, LitEventSystem* event_system);
void lit_free_event_system(LitEventSystem* event_system);
void lit_register_event(LitState* state, LitValue callback, uint64_t time);
void lit_event_loop(LitState* state);

#endif