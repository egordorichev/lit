#ifndef LIT_CLI_H
#define LIT_CLI_H

#ifdef LIT_OS_UNIX_LIKE
#define LIT_USE_LIBREADLINE
#endif

#ifdef LIT_USE_LIBREADLINE
#include <readline/readline.h>
#include <readline/history.h>
#else
#define LIT_REPL_INPUT_MAX 1024
#endif

#define LIT_EXIT_CODE_ARGUMENT_ERROR 1
#define LIT_EXIT_CODE_MEM_LEAK 2
#define LIT_EXIT_CODE_RUNTIME_ERROR 70
#define LIT_EXIT_CODE_COMPILE_ERROR 65

#endif