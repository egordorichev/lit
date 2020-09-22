#ifndef LIT_PREPROCESSOR_H
#define LIT_PREPROCESSOR_H

#include <lit/lit_predefines.h>
#include <lit/util/lit_table.h>

typedef struct sLitPreprocessor {
	LitState* state;
	LitTable defined;

	/*
	 * A little bit dirty hack:
	 * We need to store pointers (8 bytes in size),
	 * and so that we don't have to declare a new
	 * array type, that we will use only once,
	 * I'm using LitValues here, because LitValue
	 * also has the same size (8 bytes)
	 */
	LitValues open_ifs;
} sLitPreprocessor;

void lit_init_preprocessor(LitState* state, LitPreprocessor* preprocessor);
void lit_free_preprocessor(LitPreprocessor* preprocessor);
void lit_add_definition(LitState* state, const char* name);

bool lit_preprocess(LitPreprocessor* preprocessor, char* source);

#endif