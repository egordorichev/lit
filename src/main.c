#include <lit/vm/lit_vm.h>
#include <lit/scanner/lit_scanner.h>

#include <stdio.h>
#include <lit/std/lit_core.h>
#include <lit/api/lit_api.h>

static int run_repl() {
	LitState* state = lit_new_state();
	lit_open_libraries(state);

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

	lit_open_libraries(state);
	lit_interpret_file(state, file);

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