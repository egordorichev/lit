#ifndef LIT_TABLE_H
#define LIT_TABLE_H

#include <lit/lit_common.h>
#include <lit/vm/lit_value.h>

#define TABLE_MAX_LOAD 0.75

typedef struct {
	LitString* key;
	LitValue value;
} LitTableEntry;

typedef struct {
	uint count;
	uint capacity;

	LitTableEntry* entries;
} LitTable;

void lit_init_table(LitTable* table);
void lit_free_table(LitState* state, LitTable* table);

bool lit_table_set(LitState* state, LitTable* table, LitString* key, LitValue value);
bool lit_table_get(LitTable* table, LitString* key, LitValue* value);
bool lit_table_delete(LitTable* table, LitString* key);
LitString* lit_table_find_string(LitTable* table, const char* chars, uint length, uint32_t hash);
void lit_table_add_all(LitState* state, LitTable* from, LitTable* to);

#endif