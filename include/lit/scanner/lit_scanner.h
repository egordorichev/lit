#ifndef LIT_SCANNER_H
#define LIT_SCANNER_H

#include <lit/lit_common.h>
#include <lit/scanner/lit_token.h>
#include <lit/lit_predefines.h>

typedef struct sLitScanner {
	const char* start;
	const char* current;

	uint line;
} sLitScanner;

void lit_setup_scanner(LitScanner* scanner, const char* source);
LitToken lit_scan_token(LitScanner* scanner);

#endif