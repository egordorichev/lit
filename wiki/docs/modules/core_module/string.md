# String
LIT_INHERIT_CLASS(state->object_class)
LIT_BIND_CONSTRUCTOR(invalid_constructor)

LIT_BIND_METHOD("+", string_plus)
LIT_BIND_METHOD("toString", string_toString)
LIT_BIND_METHOD("toNumber", string_toNumber)
LIT_BIND_METHOD("toUpperCase", string_toUpperCase)
LIT_BIND_METHOD("toLowerCase", string_toLowerCase)
LIT_BIND_METHOD("contains", string_contains)
LIT_BIND_METHOD("startsWith", string_startsWith)
LIT_BIND_METHOD("endsWith", string_endsWith)
LIT_BIND_METHOD("replace", string_replace)
LIT_BIND_METHOD("substring", string_substring)
LIT_BIND_METHOD("iterator", string_iterator)
LIT_BIND_METHOD("iteratorValue", string_iteratorValue)
LIT_BIND_METHOD("[]", string_subscript)
LIT_BIND_METHOD("<", string_less)
LIT_BIND_METHOD(">", string_greater)

LIT_BIND_GETTER("length", string_length)