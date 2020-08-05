#include <lit/lit.h>
#include <lit/vm/lit_vm.h>
#include <lit/std/lit_core.h>
#include <lit/scanner/lit_scanner.h>

#include <stdio.h>

static int run_repl() {
	LitState* state = lit_new_state();
	lit_open_libraries(state);

	printf("lit v%s\n", LIT_VERSION_STRING);
	char line[1024];

	while (true) {
		printf("> ");

		if (!fgets(line, sizeof(line), stdin)) {
			printf("\n");
			break;
		}

		LitInterpretResult result = lit_interpret(state, "repl", line);

		if (result.type == INTERPRET_OK && result.result != NULL_VALUE) {
			printf("%s\n", lit_to_string(state, result.result)->chars);
		}
	}

	int64_t amount = lit_free_state(state);

	if (amount != 0) {
		fprintf(stderr, "Error: memory leak of %ld bytes!\n", amount);
		return 1;
	}

	return 0;
}

static int run_file(const char* file) {
	LitState* state = lit_new_state();

	lit_open_libraries(state);
	LitInterpretResult result = lit_interpret_file(state, file);

	int64_t amount = lit_free_state(state);

	if (amount != 0) {
		fprintf(stderr, "Error: memory leak of %ld bytes!\n", amount);
		return 1;
	}

	if (result.type == INTERPRET_OK) {
		return 0;
	}

	return result.type == INTERPRET_RUNTIME_ERROR ? 70 : 65;
}

int main(int argc, char* argv[]) {
	if (argc != 2) {
		return run_repl();
	}

	return run_file(argv[1]);
}