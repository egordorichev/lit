#include "lit/event/lit_event.h"
#include "lit/mem/lit_mem.h"
#include "lit/api/lit_calls.h"

#include <sys/time.h>

void lit_init_event_system(LitState* state, LitEventSystem* event_system) {
	event_system->events = NULL;
	event_system->last_event = NULL;
}

void lit_free_event_system(LitEventSystem* event_system) {
	event_system->events = NULL;
	event_system->last_event = NULL;
}

uint64_t millis() {
	struct timeval tv;
	gettimeofday(&tv,NULL);

	return (((uint64_t) tv.tv_sec) * 1000) + (tv.tv_usec / 1000);
}

void lit_register_event(LitState* state, LitValue callback, uint64_t time) {
	LitEventSystem* event_system = state->event_system;
	LitEvent* event = lit_reallocate(state, NULL, 0, sizeof(LitEvent));

	event->expire_time = millis() + time;
	event->callback = callback;
	event->next = NULL;
	event->previous = event_system->last_event;

	if (event_system->last_event == NULL) {
		event_system->events = event;
	} else {
		event_system->last_event->next = event;
	}

	event_system->last_event = event;
}

void lit_event_loop(LitState* state) {
	LitEventSystem* event_system = state->event_system;

	while (event_system->events != NULL) {
		LitEvent* event = event_system->events;

		while (event != NULL) {
			if (millis() >= event->expire_time) {
				LitEvent* next_event = event->next;

				if (event->previous != NULL) {
					event->previous->next = next_event;
				}

				if (event == event_system->events) {
					event_system->events = next_event;
				}

				if (event == event_system->last_event) {
					event_system->last_event = NULL;
				}

				lit_call(state, event->callback, NULL, 0);
				lit_reallocate(state, event, sizeof(LitEvent), 0);

				event = next_event;
			} else {
				event = event->next;
			}
		}
	}
}