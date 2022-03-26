# Range
LIT_INHERIT_CLASS(state->object_class)
LIT_BIND_CONSTRUCTOR(invalid_constructor)

LIT_BIND_METHOD("iterator", range_iterator)
LIT_BIND_METHOD("iteratorValue", range_iteratorValue)
LIT_BIND_METHOD("toString", range_toString)

LIT_BIND_FIELD("from", range_from, range_set_from)
LIT_BIND_FIELD("to", range_to, range_set_to)
LIT_BIND_GETTER("length", range_length)