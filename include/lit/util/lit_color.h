#ifndef LIT_COLOR_H
#define LIT_COLOR_H

#include "lit_config.h"

#if !defined(LIT_DISABLE_COLOR) && !defined(LIT_ENABLE_COLOR) && !(defined(LIT_OS_WINDOWS) || defined(EMSCRIPTEN))
#define LIT_ENABLE_COLOR
#endif

#ifdef LIT_ENABLE_COLOR
	#define COLOR_RESET "\x1B[0m"
	#define COLOR_RED "\x1B[31m"
	#define COLOR_GREEN "\x1B[32m"
	#define COLOR_YELLOW "\x1B[33m"
	#define COLOR_BLUE "\x1B[34m"
	#define COLOR_MAGENTA "\x1B[35m"
	#define COLOR_CYAN "\x1B[36m"
	#define COLOR_WHITE "\x1B[37m"
#else
	#define COLOR_RESET ""
	#define COLOR_RED ""
	#define COLOR_GREEN ""
	#define COLOR_YELLOW ""
	#define COLOR_BLUE ""
	#define COLOR_MAGENTA ""
	#define COLOR_CYAN ""
	#define COLOR_WHITE ""
#endif
#endif