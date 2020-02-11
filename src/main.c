#include <lit/vm/lit_chunk.h>
#include <lit/vm/lit_vm.h>
#include <lit/util/lit_fs.h>
#include <lit/scanner/lit_scanner.h>

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
	const char* source = lit_read_file(file);

	if (source == NULL) {
		printf("Failed top open file '%s'\n", file);
		return 1;
	}

	LitState* state = lit_new_state();

	lit_interpret(state, file, source);
	int64_t amount = lit_free_state(state);

	if (amount != 0) {
		fprintf(stderr, "Error: memory leak of %ld bytes!\n", amount);
	}

	free(source);
}

int main(int argc, char* argv[]) {
	if (argc != 2) {
		return run_repl();
	}

	return run_file(argv[1]);
}