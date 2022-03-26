# Function
LIT_INHERIT_CLASS(state->object_class)
LIT_BIND_CONSTRUCTOR(invalid_constructor)

LIT_BIND_METHOD("toString", function_toString)
LIT_BIND_GETTER("name", function_name)