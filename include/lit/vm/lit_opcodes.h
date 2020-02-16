OPCODE(POP, -1)
OPCODE(POP_MULTIPLE, 0) // Variyng stack effect
OPCODE(RETURN, -2)
OPCODE(CONSTANT, 1)
OPCODE(CONSTANT_LONG, 1)
OPCODE(TRUE, 1)
OPCODE(FALSE, 1)
OPCODE(NULL, 1)
OPCODE(NEGATE, 0)
OPCODE(NOT, 0)

OPCODE(ADD, -1)
OPCODE(SUBTRACT, -1)
OPCODE(MULTIPLY, -1)
OPCODE(DIVIDE, -1)
OPCODE(MOD, -1)

OPCODE(EQUAL, -1)
OPCODE(NOT_EQUAL, -1)
OPCODE(GREATER, -1)
OPCODE(GREATER_EQUAL, -1)
OPCODE(LESS, -1)
OPCODE(LESS_EQUAL, -1)

OPCODE(SET_GLOBAL, -1)
OPCODE(GET_GLOBAL, 1)

OPCODE(SET_LOCAL, 0)
OPCODE(GET_LOCAL, 1)
OPCODE(SET_LOCAL_LONG, 0)
OPCODE(GET_LOCAL_LONG, 1)

OPCODE(SET_PRIVATE, 0)
OPCODE(GET_PRIVATE, 1)
OPCODE(SET_PRIVATE_LONG, 0)
OPCODE(GET_PRIVATE_LONG, 1)

OPCODE(SET_UPVALUE, 0)
OPCODE(GET_UPVALUE, 1)

OPCODE(JUMP_IF_FALSE, 0)
OPCODE(JUMP_IF_NULL, 0)
OPCODE(JUMP, 0)
OPCODE(JUMP_BACK, 0)

OPCODE(CALL, 0) // Varying stack effect

OPCODE(REQUIRE, 1)
OPCODE(CLOSURE, 0)
OPCODE(CLOSE_UPVALUE, -1)

OPCODE(CLASS, -1)
OPCODE(GET_FIELD, 0)
OPCODE(SET_FIELD, -1)