#ifndef LIT_SCANNER_H
#define LIT_SCANNER_H

#include <lit/lit_common.h>
#include <lit/scanner/lit_token.h>

typedef struct {
	const char* start;
	const char* current;

	uint line;
} LitScanner;

void lit_setup_scanner(LitScanner* scanner, const char* source);
LitToken lit_scan_token(LitScanner* scanner);

#endif