OPCODE(MOVE, "MOVE", LIT_INSTRUCTION_ABC) // R(A) := R(B)
OPCODE(LOAD_CONSTANT, "LOAD_CONSTANT", LIT_INSTRUCTION_ABX) // R(A) := C(Bx)
OPCODE(LOAD_NULL, "LOAD_NULL", LIT_INSTRUCTION_ABC) // R(A) := null
OPCODE(LOAD_BOOL, "LOAD_BOOL", LIT_INSTRUCTION_ABC) // R(A) := (bool) B

OPCODE(RETURN, "RETURN", LIT_INSTRUCTION_ABC) // return R(A), ..., R(A + B - 2)

OPCODE(ADD, "ADD", LIT_INSTRUCTION_ABC) // R(A) := RC(B) + RC(C)
OPCODE(SUBTRACT, "SUBTRACT", LIT_INSTRUCTION_ABC) // R(A) := RC(B) - RC(C)
OPCODE(MULTIPLY, "MULTIPLY", LIT_INSTRUCTION_ABC) // R(A) := RC(B) * RC(C)
OPCODE(DIVIDE, "DIVIDE", LIT_INSTRUCTION_ABC) // R(A) := RC(B) / RC(C)

OPCODE(JUMP, "JUMP", LIT_INSTRUCTION_ASBX) // PC += sBx

OPCODE(EQUAL, "EQUAL", LIT_INSTRUCTION_ABC) // R(A) := ((R(B & 0xff) == RC(C)) ~= CB(B))
OPCODE(LESS, "LESS", LIT_INSTRUCTION_ABC) // R(A) := ((R(B & 0xff) < RC(C)) ~= CB(B))
OPCODE(LESS_EQUAL, "LESS_EQUAL", LIT_INSTRUCTION_ABC) // R(A) := ((R(B & 0xff) <= RC(C)) ~= CB(B))

OPCODE(NEGATE, "NEGATE", LIT_INSTRUCTION_ABC) // R(A) := -RC(B)
OPCODE(NOT, "NOT", LIT_INSTRUCTION_ABC) // R(A) := !RC(B)

OPCODE(SET_GLOBAL, "SET_GLOBAL", LIT_INSTRUCTION_ABX) // G[C(Bx)] := R(A)
OPCODE(GET_GLOBAL, "GET_GLOBAL", LIT_INSTRUCTION_ABX) // R(A) := G[C(Bx)]