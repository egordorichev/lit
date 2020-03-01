#include <lit/std/lit_file.h>
#include <lit/api/lit_api.h>
#include <lit/lit_config.h>

#include <sys/types.h>
#include <sys/stat.h>

#ifdef LIT_OS_WINDOWS
#define stat _stat
#endif

/*
 * File
 */

LIT_METHOD(file_exists) {
	const char* file_name = LIT_CHECK_STRING(0);
	struct stat buffer;

	return BOOL_VALUE(stat(file_name, &buffer) == 0);
}

/*
 * Directory
 */

void lit_open_file_library(LitState* state) {
	LIT_BEGIN_CLASS("File")
		LIT_BIND_STATIC_METHOD("exists", file_exists)
	LIT_END_CLASS()

	LIT_BEGIN_CLASS("Directory")

	LIT_END_CLASS()
}