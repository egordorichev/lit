OPCODE(MOVE, "MOVE", LIT_INSTRUCTION_ABC) // R(A) := RC(B)
OPCODE(LOAD_NULL, "LOAD_NULL", LIT_INSTRUCTION_ABC) // R(A) := null
OPCODE(LOAD_BOOL, "LOAD_BOOL", LIT_INSTRUCTION_ABC) // R(A) := (bool) B

OPCODE(RETURN, "RETURN", LIT_INSTRUCTION_ABC) // return R(A)

OPCODE(ADD, "ADD", LIT_INSTRUCTION_ABC) // R(A) := RC(B) + RC(C)
OPCODE(SUBTRACT, "SUBTRACT", LIT_INSTRUCTION_ABC) // R(A) := RC(B) - RC(C)
OPCODE(MULTIPLY, "MULTIPLY", LIT_INSTRUCTION_ABC) // R(A) := RC(B) * RC(C)
OPCODE(DIVIDE, "DIVIDE", LIT_INSTRUCTION_ABC) // R(A) := RC(B) / RC(C)

OPCODE(JUMP, "JUMP", LIT_INSTRUCTION_ASBX) // PC += sBx
OPCODE(TRUE_JUMP, "TRUE_JUMP", LIT_INSTRUCTION_ABX) // if (R(A)) PC += Bx
OPCODE(FALSE_JUMP, "FALSE_JUMP", LIT_INSTRUCTION_ABX) // if (not R(A)) PC += Bx
OPCODE(NON_NULL_JUMP, "NON_NULL_JUMP", LIT_INSTRUCTION_ABX) // if (R(A) != null) PC += Bx

OPCODE(EQUAL, "EQUAL", LIT_INSTRUCTION_ABC) // R(A) := RC(B) == RC(C)
OPCODE(LESS, "LESS", LIT_INSTRUCTION_ABC) // R(A) := RC(B) < RC(C)
OPCODE(LESS_EQUAL, "LESS_EQUAL", LIT_INSTRUCTION_ABC) // R(A) := RC(B) <= RC(C)

OPCODE(NEGATE, "NEGATE", LIT_INSTRUCTION_ABC) // R(A) := -RC(B)
OPCODE(NOT, "NOT", LIT_INSTRUCTION_ABC) // R(A) := !RC(B)

OPCODE(SET_GLOBAL, "SET_GLOBAL", LIT_INSTRUCTION_ABX) // G[C(A)] := RC(BX)
OPCODE(GET_GLOBAL, "GET_GLOBAL", LIT_INSTRUCTION_ABX) // R(A) := G[C(Bx)]

OPCODE(CALL, "CALL", LIT_INSTRUCTION_ABC) // R(A), ..., R(A + C - 2) := R(A)(R(A + 1), ..., R(A + B - 1))