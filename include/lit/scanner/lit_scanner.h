#ifndef LIT_SCANNER_H
#define LIT_SCANNER_H

#include <lit/lit_common.h>
#include <lit/lit_predefines.h>
#include <lit/state/lit_state.h>
#include <lit/scanner/lit_token.h>

typedef struct sLitScanner {
	uint line;
	const char* start;
	const char* current;
	const char* file_name;
} sLitScanner;

void lit_setup_scanner(LitScanner* scanner, const char* file_name, const char* source);
LitToken lit_scan_token(LitScanner* scanner);

#endif