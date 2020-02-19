#include <lit/vm/lit_chunk.h>
#include <lit/vm/lit_vm.h>
#include <lit/util/lit_fs.h>
#include <lit/scanner/lit_scanner.h>
#include <lit/api/lit_api.h>

#include <stdlib.h>
#include <stdio.h>

static int run_repl() {
	LitState* state = lit_new_state();
	char line[1024];

	while (true) {
		printf("> ");

		if (!fgets(line, sizeof(line), stdin)) {
			printf("\n");
			break;
		}

		lit_interpret(state, "repl", line);
	}

	int64_t amount = lit_free_state(state);

	if (amount != 0) {
		fprintf(stderr, "Error: memory leak of %ld bytes!\n", amount);
	}
}

static int run_file(const char* file) {
	LitState* state = lit_new_state();
	// lit_init_api(state);
	lit_interpret_file(state, file);

	/*LitValue global = lit_get_global(state, CONST_STRING(state, "printTest"));

	if (IS_FUNCTION(global)) {
		LitValue arg = NUMBER_VALUE(10);
		lit_print_value(lit_call(state, global, &arg, 1).result);
	}

	lit_free_api(state);*/
	int64_t amount = lit_free_state(state);

	if (amount != 0) {
		fprintf(stderr, "Error: memory leak of %ld bytes!\n", amount);
	}
}

int main(int argc, char* argv[]) {
	if (argc != 2) {
		return run_repl();
	}

	return run_file(argv[1]);
}