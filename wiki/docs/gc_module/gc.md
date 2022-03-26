# GC

LIT_BIND_STATIC_GETTER("memoryUsed", gc_memory_used)
LIT_BIND_STATIC_GETTER("nextRound", gc_next_round)

LIT_BIND_STATIC_METHOD("trigger", gc_trigger)