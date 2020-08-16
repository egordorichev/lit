#ifndef LIT_FS_H
#define LIT_FS_H

#include <lit/lit_common.h>
#include <lit/lit_predefines.h>
#include <lit/vm/lit_object.h>

#include <stdio.h>

#define BYTECODE_MAGICAL_NUMBER 6932

const char* lit_read_file(const char* path);

void lit_write_uint8_t(FILE* file, uint8_t byte);
void lit_write_uint16_t(FILE* file, uint16_t byte);
void lit_write_uint32_t(FILE* file, uint32_t byte);
void lit_write_double(FILE* file, double byte);
void lit_write_string(FILE* file, LitString* string);

uint8_t lit_read_uint8_t(FILE* file);
uint16_t lit_read_uint16_t(FILE* file);
uint32_t lit_read_uint32_t(FILE* file);
double lit_read_double(FILE* file);
LitString* lit_read_string(LitState* state, FILE* file);

typedef struct {
	const char* source;
	uint position;
} LitEmulatedFile;

void lit_init_emulated_file(LitEmulatedFile* file, const char* source);

uint8_t lit_read_euint8_t(LitEmulatedFile* file);
uint16_t lit_read_euint16_t(LitEmulatedFile* file);
uint32_t lit_read_euint32_t(LitEmulatedFile* file);
double lit_read_edouble(LitEmulatedFile* file);
LitString* lit_read_estring(LitState* state, LitEmulatedFile* file);

bool lit_save_module(LitModule* module, const char* output_file);
LitModule* lit_load_module(LitState* state, const char* input);

#endif