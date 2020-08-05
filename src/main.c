#include <lit/lit.h>
#include <lit/vm/lit_vm.h>
#include <lit/std/lit_core.h>
#include <lit/scanner/lit_scanner.h>

#include <stdio.h>

#define EXIT_CODE_ARGUMENT_ERROR 1
#define EXIT_CODE_MEM_LEAK 2
#define EXIT_CODE_RUNTIME_ERROR 70
#define EXIT_CODE_COMPILE_ERROR 65

static int run_repl() {
	LitState* state = lit_new_state();
	lit_open_libraries(state);

	printf("lit v%s, developed by @egordorichev\n", LIT_VERSION_STRING);
	char line[1024];

	while (true) {
		printf("%s>%s ", COLOR_BLUE, COLOR_RESET);

		if (!fgets(line, sizeof(line), stdin)) {
			printf("\n");
			break;
		}

		LitInterpretResult result = lit_interpret(state, "repl", line);

		if (result.type == INTERPRET_OK && result.result != NULL_VALUE) {
			printf("%s%s%s\n", COLOR_GREEN, lit_to_string(state, result.result)->chars, COLOR_RESET);
		}
	}

	int64_t amount = lit_free_state(state);

	if (amount != 0) {
		fprintf(stderr, "Error: memory leak of %ld bytes!\n", amount);
		return 1;
	}

	return 0;
}

static void show_help() {
	printf("lit [options] [file]\n");
	printf("\t-e --eval [string]\tRuns the given code string.\n");
	printf("\t-p --pass [args]\tPasses the rest of the arguments to the script.\n");
	printf("\t-h --help\t\tI wonder, what this option does.\n");
	printf("\tIf no arguments are provided, interactive shell will start.\n");
}

static bool match_arg(const char* arg, const char* a, const char* b) {
	return strcmp(arg, a) == 0 || strcmp(arg, b) == 0;
}

int main(int argc, const char* argv[]) {
	if (argc == 1) {
		run_repl();
		return 0;
	}

	LitState* state = lit_new_state();
	lit_open_libraries(state);

	const char* file_to_run = NULL;
	LitInterpretResultType result = INTERPRET_OK;

	for (int i = 1; i < argc; i++) {
		const char* arg = argv[i];

		if (arg[0] == '-') {
			if (match_arg(arg, "-e", "--eval")) {
				// It takes an extra argument, count it or we will use it as the file name to run :P
				i++;
			} else if (match_arg(arg, "-p", "--pass")) {
				// The rest of the args go to the script, go home pls
				break;
			}

			continue;
		}

		if (file_to_run != NULL) {
			printf("File to run was already specified (%s).\n", file_to_run);
			return EXIT_CODE_ARGUMENT_ERROR;
		}

		file_to_run = arg;
	}

	for (int i = 1; i < argc; i++) {
		int args_left = argc - i - 1;
		const char* arg = argv[i];

		if (match_arg(arg, "-e", "--eval")) {
			if (args_left == 0) {
				printf("Expected code to run for the eval argument.\n");
				return EXIT_CODE_ARGUMENT_ERROR;
			}

			result = lit_interpret(state, file_to_run == NULL ? "repl" : file_to_run, argv[++i]).type;

			if (result != INTERPRET_OK) {
				break;
			}
		} else if (match_arg(arg, "-h", "--help")) {
			show_help();
		} else if (match_arg(arg, "-p", "--pass")) {
			// TODO
			break;
		} else if (arg[0] == '-') {
			printf("Unknown argument '%s', run 'lit --help' for help.\n", arg);
			return EXIT_CODE_ARGUMENT_ERROR;
		}
	}

	if (file_to_run != NULL) {
		result = lit_interpret_file(state, file_to_run).type;
	}

	int64_t amount = lit_free_state(state);

	if (amount != 0) {
		fprintf(stderr, "Error: memory leak of %ld bytes!\n", amount);
		return EXIT_CODE_MEM_LEAK;
	}

	if (result != INTERPRET_OK) {
		return result == INTERPRET_RUNTIME_ERROR ? EXIT_CODE_RUNTIME_ERROR : EXIT_CODE_COMPILE_ERROR;
	}

	return 0;
}