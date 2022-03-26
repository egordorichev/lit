# Fiber
LIT_INHERIT_CLASS(state->object_class)
LIT_BIND_CONSTRUCTOR(fiber_constructor)

LIT_BIND_PRIMITIVE("run", fiber_run)
LIT_BIND_PRIMITIVE("try", fiber_try)
LIT_BIND_GETTER("done", fiber_done)
LIT_BIND_GETTER("error", fiber_error)

LIT_BIND_STATIC_PRIMITIVE("yield", fiber_yield)
LIT_BIND_STATIC_PRIMITIVE("yeet", fiber_yeet)
LIT_BIND_STATIC_PRIMITIVE("abort", fiber_abort)
LIT_BIND_STATIC_GETTER("current", fiber_current)