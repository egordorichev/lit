#ifndef LIT_MEM_H
#define LIT_MEM_H

#include <lit/state/lit_state.h>
#include <lit/lit_common.h>

#define LIT_GROW_CAPACITY(capacity) ((capacity) < 8 ? 8 : (capacity) * 2)
#define LIT_GROW_ARRAY(state, previous, type, old_count, count) \
    (type*) lit_reallocate(state, previous, sizeof(type) * (old_count), sizeof(type) * (count))

#define LIT_FREE_ARRAY(state, type, pointer, old_count) lit_reallocate(state, pointer, sizeof(type) * (old_count), 0)

#define LIT_ALLOCATE(state, type, count) (type*) lit_reallocate(state, NULL, 0, sizeof(type) * (count))
#define LIT_FREE(state, type, pointer) lit_reallocate(state, pointer, sizeof(type), 0)

void* lit_reallocate(LitState* state, void* pointer, size_t old_size, size_t new_size);
void lit_free_objects(LitState* state, LitObject* objects);

#endif