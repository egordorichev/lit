#include <lit/lit.h>
#include <lit/vm/lit_vm.h>
#include <lit/std/lit_core.h>
#include <lit/scanner/lit_scanner.h>
#include <lit/util/lit_fs.h>
#include <lit/optimizer/lit_optimizer.h>

#include <stdio.h>

#define EXIT_CODE_ARGUMENT_ERROR 1
#define EXIT_CODE_MEM_LEAK 2
#define EXIT_CODE_RUNTIME_ERROR 70
#define EXIT_CODE_COMPILE_ERROR 65

static void run_repl(LitState* state) {
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
}

static void show_help() {
	printf("lit [options] [files]\n");
	printf("\t-o --output [file]\tInstead of running the file the compiled bytecode will be saved.\n");
	printf("\t-O[name] [string]\tEnables given optimization. For the list of aviable optimizations run with -Ohelp\n");
	printf("\t-e --eval [string]\tRuns the given code string.\n");
	printf("\t-p --pass [args]\tPasses the rest of the arguments to the script.\n");
	printf("\t-i --interactive\tStarts an interactive shell.\n");
	printf("\t-h --help\t\tI wonder, what this option does.\n");
	printf("\tIf no code to run is provided, lit will try to run either main.lbc or main.lit and, if fails, default to an interactive shell will start.\n");
}

static void show_optimization_help() {
	printf("Lit has a lot of optimzations. You can turn each one on or off or use a predefined optimization level to set them to a default value.\n");
	printf("The more optimizations are enabled, the longer it takes to compile, but the program should run better. So I recommend using low optimization for development and high optimization for release.\n");
	printf("\nTo enable an optimization, run lit with argument -O[optimization], for example -Oconstant-folding. Using flag -Oall will enable all optimizations.\n");
	printf("To disable an optimization, run lit with argument -Ono-[optimization], for example -Ono-constant-folding. Using flag -Oall will disable all optimizations.\n");
	printf("Here is a list of all supported optimizations:\n\n");

	for (int i = 0; i < OPTIMIZATION_TOTAL; i++) {
		printf("\t%s\t\t%s\n", lit_get_optimization_name((LitOptimization) i), lit_get_optimization_description((LitOptimization) i));
	}

	printf("\nIf you want to use a predefined optimization level (recommended), run lit with argument -O[optimization level], for example -O1.");
	// TODO: list all levels
}

static bool match_arg(const char* arg, const char* a, const char* b) {
	return strcmp(arg, a) == 0 || strcmp(arg, b) == 0;
}

int main(int argc, const char* argv[]) {
	LitState* state = lit_new_state();
	lit_open_libraries(state);

	char* files_to_run[argc - 2];
	uint num_files_to_run = 0;

	LitInterpretResultType result = INTERPRET_OK;

	for (int i = 1; i < argc; i++) {
		const char* arg = argv[i];

		if (arg[0] == '-') {
			if (match_arg(arg, "-e", "--eval") || match_arg(arg, "-o", "--output")) {
				// It takes an extra argument, count it or we will use it as the file name to run :P
				i++;
			} else if (match_arg(arg, "-p", "--pass")) {
				// The rest of the args go to the script, go home pls
				break;
			}

			continue;
		}

		files_to_run[num_files_to_run++] = (char*) arg;
	}

	LitArray* arg_array = NULL;
	bool show_repl = false;
	bool evaled = false;
	char* bytecode_file = NULL;

	for (int i = 1; i < argc; i++) {
		int args_left = argc - i - 1;
		const char* arg = argv[i];

		if (strcmp(arg, "-O") > 0) {
			bool enable_optimization = true;
			char* optimization_name;

			// -Ono-whatever
			if (strcmp((char*) (arg + 2), "no-") > 0) {
				enable_optimization = false;
				optimization_name = (char*) (arg + 5);
			} else {
				optimization_name = (char*) (arg + 2);
			}

			if (enable_optimization && strcmp(optimization_name, "help") == 0) {
				show_optimization_help();
			} else if (strcmp(optimization_name, "all") == 0) {
				lit_set_all_optimization_enabled(enable_optimization);
			} else {
				bool found = false;

				// Yes I know, this is not the fastest way, and what now?
				for (uint j = 0; j < OPTIMIZATION_TOTAL; j++) {
					if (strcmp(lit_get_optimization_name((LitOptimization) j), optimization_name) == 0) {
						found = true;
						lit_set_optimization_enabled((LitOptimization) j, enable_optimization);

						break;
					}
				}

				if (!found) {
					printf("Unknown optimization '%s'. Run with -Ohelp for a list of all optimizations.\n", optimization_name);
					return EXIT_CODE_ARGUMENT_ERROR;
				}
			}
		} else if (match_arg(arg, "-e", "--eval")) {
			evaled = true;

			if (args_left == 0) {
				printf("Expected code to run for the eval argument.\n");
				return EXIT_CODE_ARGUMENT_ERROR;
			}

			result = lit_interpret(state, num_files_to_run == 0 ? "repl" : files_to_run[0], argv[++i]).type;

			if (result != INTERPRET_OK) {
				break;
			}
		} else if (match_arg(arg, "-h", "--help")) {
			show_help();
		} else if (match_arg(arg, "-i", "--interactive")) {
			show_repl = true;
		} else if (match_arg(arg, "-o", "--output")) {
			if (args_left == 0) {
				printf("Expected file name where to save the bytecode.\n");
				return EXIT_CODE_ARGUMENT_ERROR;
			}

			bytecode_file = (char*) argv[++i];
		} else if (match_arg(arg, "-p", "--pass")) {
			arg_array = lit_create_array(state);

			for (int j = 0; j < args_left; j++) {
				const char* arg_string = argv[i + j + 1];
				lit_values_write(state, &arg_array->values, OBJECT_CONST_STRING(state, arg_string));
			}

			lit_set_global(state, CONST_STRING(state, "args"), OBJECT_VALUE(arg_array));
			break;
		} else if (arg[0] == '-') {
			printf("Unknown argument '%s', run 'lit --help' for help.\n", arg);
			return EXIT_CODE_ARGUMENT_ERROR;
		}
	}

	if (num_files_to_run > 0) {
		if (bytecode_file != NULL) {
			if (!lit_compile_and_save_files(state, files_to_run, num_files_to_run, bytecode_file)) {
				result = INTERPRET_COMPILE_ERROR;
			}
		} else {
			if (arg_array == NULL) {
				arg_array = lit_create_array(state);
			}

			lit_set_global(state, CONST_STRING(state, "args"), OBJECT_VALUE(arg_array));

			for (uint i = 0; i < num_files_to_run; i++) {
				result = lit_interpret_file(state, files_to_run[i]).type;

				if (result != INTERPRET_OK) {
					break;
				}
			}
		}
	}

	if (show_repl) {
		run_repl(state);
	} else if (!evaled) {
		if (lit_file_exists("main.lbc")) {
			result = lit_interpret_file(state, "main.lbc").type;
		} else if (lit_file_exists("main.lit")) {
			result = lit_interpret_file(state, "main.lit").type;
		} else {
			run_repl(state);
		}
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