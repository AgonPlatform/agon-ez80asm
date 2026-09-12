#ifndef INSTRUCTION_H
#define INSTRUCTION_H

#include "config.h"
#include "defines.h"

instruction_t * instruction_lookup(const char *name);
void initInstructionTable(void);
void emit_instruction(const operandlist_t *list);
uint8_t get_immediate_size(uint8_t suffix);

extern operandlist_t operands_call[];
extern instruction_t *instruction_table[INSTRUCTION_HASHTABLESIZE];

#endif // INSTRUCTION_H
