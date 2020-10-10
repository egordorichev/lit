#include "util/lit_table.h"
#include "mem/lit_mem.h"
#include "vm/lit_object.h"

#include <string.h>

void lit_init_table(LitTable* table) {
	table->capacity = -1;
	table->count = 0;
	table->entries = NULL;
}

void lit_free_table(LitState* state, LitTable* table) {
	if (table->capacity > 0) {
		LIT_FREE_ARRAY(state, LitTableEntry, table->entries, table->capacity + 1);
	}

	lit_init_table(table);
}

static LitTableEntry* find_entry(LitTableEntry* entries, int capacity, LitString* key) {
	uint32_t index = key->hash % capacity;
	LitTableEntry* tombstone = NULL;

	while (true) {
		LitTableEntry* entry = &entries[index];

		if (entry->key == NULL) {
			if (IS_NULL(entry->value)) {
				return tombstone != NULL ? tombstone : entry;
			} else if (tombstone == NULL) {
				tombstone = entry;
			}
		} if (entry->key == key) {
			return entry;
		}

		index = (index + 1) % capacity;
	}
}

static void adjust_capacity(LitState* state, LitTable* table, int capacity) {
	LitTableEntry* entries = LIT_ALLOCATE(state, LitTableEntry, capacity + 1);

	for (int i = 0; i <= capacity; i++) {
		entries[i].key = NULL;
		entries[i].value = NULL_VALUE;
	}

	table->count = 0;

	for (int i = 0; i <= table->capacity; i++) {
		LitTableEntry* entry = &table->entries[i];

		if (entry->key == NULL) {
			continue;
		}

		LitTableEntry* destination = find_entry(entries, capacity, entry->key);

		destination->key = entry->key;
		destination->value = entry->value;

		table->count++;
	}

	LIT_FREE_ARRAY(state, LitTableEntry, table->entries, table->capacity + 1);

	table->capacity = capacity;
	table->entries = entries;
}

bool lit_table_set(LitState* state, LitTable* table, LitString* key, LitValue value) {
	if (table->count + 1 > (table->capacity + 1) * TABLE_MAX_LOAD) {
		int capacity = LIT_GROW_CAPACITY(table->capacity + 1) - 1;
		adjust_capacity(state, table, capacity);
	}

	LitTableEntry* entry = find_entry(table->entries, table->capacity, key);
	bool is_new = entry->key == NULL;

	if (is_new && IS_NULL(entry->value)) {
		table->count++;
	}

	entry->key = key;
	entry->value = value;

	return is_new;
}

bool lit_table_get(LitTable* table, LitString* key, LitValue* value) {
	if (table->count == 0) {
		return false;
	}

	LitTableEntry* entry = find_entry(table->entries, table->capacity, key);

	if (entry->key == NULL) {
		return false;
	}

	*value = entry->value;
	return true;
}


bool lit_table_get_slot(LitTable* table, LitString* key, LitValue** value) {
	if (table->count == 0) {
		return false;
	}

	LitTableEntry* entry = find_entry(table->entries, table->capacity, key);

	if (entry->key == NULL) {
		return false;
	}

	*value = &entry->value;
	return true;
}

bool lit_table_delete(LitTable* table, LitString* key) {
	if (table->count == 0) {
		return false;
	}

	LitTableEntry* entry = find_entry(table->entries, table->capacity, key);

	if (entry->key == NULL) {
		return false;
	}

	entry->key = NULL;
	entry->value = BOOL_VALUE(true);

	return true;
}

LitString* lit_table_find_string(LitTable* table, const char* chars, uint length, uint32_t hash) {
	if (table->count == 0) {
		return NULL;
	}

	uint32_t index = hash % table->capacity;

	while (true) {
		LitTableEntry* entry = &table->entries[index];

		if (entry->key == NULL) {
			if (IS_NULL(entry->value)) {
				return NULL;
			}
		} else if (entry->key->length == length &&
				entry->key->hash == hash &&
				memcmp(entry->key->chars, chars, length) == 0) {

			return entry->key;
		}

		index = (index + 1) % table->capacity;
	}
}

void lit_table_add_all(LitState* state, LitTable* from, LitTable* to) {
	for (int i = 0; i <= from->capacity; i++) {
		LitTableEntry* entry = &from->entries[i];

		if (entry->key != NULL) {
			lit_table_set(state, to, entry->key, entry->value);
		}
	}
}

void lit_table_remove_white(LitTable* table) {
	for (int i = 0; i <= table->capacity; i++) {
		LitTableEntry* entry = &table->entries[i];

		if (entry->key != NULL && !entry->key->object.marked) {
			lit_table_delete(table, entry->key);
		}
	}
}

void lit_mark_table(LitVm* vm, LitTable* table) {
	for (int i = 0; i <= table->capacity; i++) {
		LitTableEntry* entry = &table->entries[i];

		lit_mark_object(vm, (LitObject*) entry->key);
		lit_mark_value(vm, entry->value);
	}
}