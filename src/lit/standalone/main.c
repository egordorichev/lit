#include <lit/lit.h>
#include <lit/cli/lit_cli.h>
#include <lit/vm/lit_vm.h>
#include <lit/std/lit_core.h>
#include <lit/scanner/lit_scanner.h>
#include <lit/util/lit_fs.h>
#include <lit/optimizer/lit_optimizer.h>

#include <signal.h>
#include <stdlib.h>

// Used for clean up on Ctrl+C / Ctrl+Z
static LitState* state;
extern char bytecode[];

void interupt_handler(int signal_id) {
	lit_free_state(state);
	exit(0);
}

int main(int argc, const char* argv[]) {
	signal(SIGINT, interupt_handler);
	signal(SIGTSTP, interupt_handler);

	state = lit_new_state();

	lit_open_libraries(state);
	lit_set_optimization_level(OPTIMIZATION_LEVEL_EXTREME);

	LitArray* arg_array = lit_create_array(state);

	for (int i = 1; i < argc; i++) {
		const char* arg_string = argv[i];
		lit_values_write(state, &arg_array->values, OBJECT_CONST_STRING(state, arg_string));
	}

	lit_set_global(state, CONST_STRING(state, "args"), OBJECT_VALUE(arg_array));
	LitInterpretResultType result = lit_interpret(state, "main", bytecode).type;

	int64_t amount = lit_free_state(state);

	if (result != INTERPRET_COMPILE_ERROR && amount != 0) {
		return LIT_EXIT_CODE_MEM_LEAK;
	}

	if (result != INTERPRET_OK) {
		return result == INTERPRET_RUNTIME_ERROR ? LIT_EXIT_CODE_RUNTIME_ERROR : LIT_EXIT_CODE_COMPILE_ERROR;
	}

	return 0;
}