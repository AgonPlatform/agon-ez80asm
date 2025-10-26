#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include "config.h"
#include "defines.h"
#include "hash.h"
#include "utils.h"
#include "macro.h"
#include "moscalls.h"
#include "globals.h"
#include "label.h"
#include "io.h"
#include "instruction.h"

// instruction hash table
instruction_t *instruction_table[INSTRUCTION_HASHTABLESIZE];

// get the number of bytes to emit from an immediate
uint8_t get_immediate_size(uint8_t suffix) {
    if(suffix) {
        if(suffix & (S_SIS|S_LIS)) return 2;
        if(suffix & (S_SIL|S_LIL)) return 3;
        error(message[ERROR_INVALIDMNEMONIC],0);
        return 0;
   }
    if(adlmode) return 3;
    else return 2;
}

uint8_t get_ddfd_prefix(uint24_t reg) {
    if(reg & (R_IX|R_IXH|R_IXL)) return 0xDD;
    if(reg & (R_IY|R_IYH|R_IYL)) return 0xFD;
    return 0;
}

void prefix_ddfd_suffix(const operandlist_t *op) {
    uint8_t prefix1, prefix2;

    if(!(op->flags & F_DDFDOK)) return;

    prefix1 = get_ddfd_prefix(operand1.reg);
    prefix2 = get_ddfd_prefix(operand2.reg);

    if((!prefix1 && prefix2) || (!operand1.indirect && prefix1 && prefix2)) {
        output.prefix1 = prefix2;
   }
    else {
        output.prefix1 = prefix1;
   }
}

void transform_instruction(operand_t *op, uint8_t type) {
    uint8_t y;
    int24_t rel;

    switch(type) {
        case TRANSFORM_NONE:
            break;
        case TRANSFORM_IR0:
            if(op->reg & (R_IXL|R_IYL)) output.opcode |= 0x01;
            break;
        case TRANSFORM_IR3:
            if(op->reg & (R_IXL|R_IYL)) output.opcode |= 0x08;
            break;
        case TRANSFORM_Z:
            output.opcode |= op->reg_index;
            break;
        case TRANSFORM_Y:
            if(op->immediate_provided) output.opcode |= (op->immediate << 3);
            else output.opcode |= (op->reg_index << 3);
            break;
        case TRANSFORM_P:
            output.opcode |= (op->reg_index << 4);
            break;
        case TRANSFORM_CC:
            output.opcode |= (op->cc_index << 3);
            break;
        case TRANSFORM_N:
            output.opcode |= op->immediate;
            op->immediate_provided = false; // no separate output for this transform
            break;
        case TRANSFORM_BIT:
            output.opcode |= (op->immediate << 3);
            op->immediate_provided = false;
            break;
        case TRANSFORM_SELECT:
            switch(op->immediate) {
                case 0:
                    y = 0;
                    break;
                case 1:
                    y = 2;
                    break;
                case 2:
                    y = 3;
                    break;
                default:
                    y = 0;
           }
            output.opcode |= (y << 3); // shift 3 lower bits 3 to the left
            op->immediate_provided = false; // no separate output for this transform
            break;
        case TRANSFORM_REL:
            if(pass == ENDPASS) {
                // label still potentially unknown in pass 1, so output the existing '0' in pass 1
                if(relocate) {
                   rel = op->immediate - (relocateBaseAddress + (address - relocateOutputBaseAddress)) - 2;
                } else {
                   rel = op->immediate - address - 2;
                }
                if((rel > 127) || (rel < -128)) {
                    error(message[ERROR_RELATIVEJUMPTOOLARGE],0);
               }
                op->immediate = ((int8_t)(rel & 0xFF));
                op->immediate_provided = true;
           }
            break;
        default:
            error(message[ERROR_TRANSFORMATION],0);
            break;
   }
    return;
}

// return ADL prefix bitfield, or 0 if none present
uint8_t getADLsuffix(void) {

    if(currentline.suffixpresent == false) return 0;

    switch(strlen(currentline.suffix)) {
        case 1: // .s or .l
            switch(tolower(currentline.suffix[0])) {
                case 's':
                    if(adlmode) return S_SIL;  // SIL
                    else return S_SIS;         // SIS
                    break;
                case 'l':
                    if(adlmode) return S_LIL;  // LIL
                    else return S_LIS;         // LIS
                    break;
                default: // illegal suffix
                    break;
           }
            break;
        case 2: // .is or .il
            if(tolower(currentline.suffix[0]) != 'i') break; // illegal suffix
            switch(tolower(currentline.suffix[1])) {
                case 's':
                    if(adlmode) return S_LIS;  // LIS
                    else return S_SIS;         // SIS
                    break;
                case 'l':
                    if(adlmode) return S_LIL;  // LIL
                    else return S_SIL;         // SIL
                    break;
                default: // illegal suffix
                    break;
           }
            break;
        case 3:
            if(tolower(currentline.suffix[1]) != 'i') break; // illegal suffix
            switch(tolower(currentline.suffix[0])) {
                case 's':
                    if(tolower(currentline.suffix[2]) == 's') return S_SIS; // SIS
                    if(tolower(currentline.suffix[2]) == 'l') return S_SIL; // SIL
                    // illegal suffix
                    break;
                case 'l':
                    if(tolower(currentline.suffix[2]) == 's') return S_LIS; // LIS
                    if(tolower(currentline.suffix[2]) == 'l') return S_LIL; // LIL
                    // illegal suffix
                    break;
                default: // illegal suffix
                    break;
           }
            break;
        default: // illegal suffix
            break;
   }
    error(message[ERROR_INVALIDSUFFIX],"%s",currentline.suffix);
    return 0;
}

void emit_instruction(const operandlist_t *list) {
    bool ddbeforeopcode; // determine position of displacement byte in case of DDCBdd/DDFDdd
    
    // Transform necessary prefix/opcode in output, according to given list and operands
    output.suffix = getADLsuffix();
    output.prefix1 = 0;
    output.prefix2 = list->prefix;
    output.opcode = list->opcode;

    if(output.suffix && !(cputype & BIT_EZ80)) {
       errorCPUtype(ERROR_INVALID_CPU_INSTRUCTION);
       return;
    }

    definelabel(address);

    // issue any warnings here
    if((list->transformA != TRANSFORM_REL) && (list->transformB != TRANSFORM_REL)) { // TRANSFORM_REL will mask to 0xFF
        if(!ignore_truncation_warnings) {
            if((list->conditionsA & IMM_N) && ((operand1.immediate > 0xFF) || (operand1.immediate < -128))) warning(message[WARNING_TRUNCATED_8BIT],"%s",operand1.immediate_name);
            if((list->conditionsB & IMM_N) && ((operand2.immediate > 0xFF) || (operand2.immediate < -128))) warning(message[WARNING_TRUNCATED_8BIT],"%s",operand2.immediate_name);
        }
    }
    if((output.suffix) && ((list->flags & output.suffix) == 0)) error(message[ERROR_ILLEGAL_SUFFIXMODE],"%s",currentline.suffix);
    if((list->flags & F_DISPB) && ((operand2.displacement < -128) || (operand2.displacement > 127))) error(message[ERROR_DISPLACEMENT_RANGE],"%d",operand2.displacement);

    // Specific checks
    if((list->conditionsA & IMM_BIT) && (operand1.immediate > 7)) error(message[ERROR_INVALIDBITNUMBER],"%s",operand1.immediate_name);
    if((list->conditionsA & IMM_NSELECT) && (operand1.immediate > 2)) error(message[ERROR_ILLEGALINTERRUPTMODE],"%s",operand1.immediate_name);
    if((list->transformA == TRANSFORM_N) && (operand1.immediate & 0x47)) error(message[ERROR_ILLEGALRESTARTADDRESS],"%s",operand1.immediate_name);

    // prepare extra DD/FD suffix if needed
    prefix_ddfd_suffix(list);
    // Transform the opcode and potential immediate values, according to the current ruleset
    transform_instruction(&operand1, (uint8_t)list->transformA);
    transform_instruction(&operand2, (uint8_t)list->transformB);
    // determine position of dd
    ddbeforeopcode = (((output.prefix1 == 0xDD) || (output.prefix1 == 0xFD)) && (output.prefix2 == 0xCB) &&
                (list->flags & (F_DISPA|F_DISPB)));
    
    // output adl suffix and any prefixes
    if(output.suffix) emit_adlsuffix_code(output.suffix);
    if(output.prefix1) emit_8bit(output.prefix1);
    if(output.prefix2) emit_8bit(output.prefix2);

    // opcode in normal position
    if(!ddbeforeopcode) emit_8bit(output.opcode);
    
    // output displacement
    if(list->flags & F_DISPA) emit_8bit(operand1.displacement);
    if(list->flags & F_DISPB) emit_8bit(operand2.displacement);
    
    // output n
    if((operand1.immediate_provided) && (list->conditionsA & IMM_N)) emit_8bit(operand1.immediate);
    if((operand2.immediate_provided) && (list->conditionsB & IMM_N)) emit_8bit(operand2.immediate);

    // opcode in DDCBdd/DFCBdd position
    if(ddbeforeopcode) emit_8bit(output.opcode);

    //output remaining immediate bytes
    if(list->conditionsA & IMM_MMN) emit_immediate(&operand1, output.suffix);
    if(list->conditionsB & IMM_MMN) emit_immediate(&operand2, output.suffix);
}

operandlist_t operands_adc[] = {
   {    R_A,               NOREQ,   R_HL,            INDIRECT,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0x00,0x8E},
   {    R_A,               NOREQ,  RS_IR,               NOREQ,  TRANSFORM_NONE, TRANSFORM_IR0,              F_DDFDOK,BIT_U80|BIT_EZ80,0x00,0x8C},
   {    R_A,               NOREQ, RS_IXY,            INDIRECT,  TRANSFORM_NONE,TRANSFORM_NONE,F_DISPB|F_DDFDOK|S_ANY,BIT_Z80,0x00,0x8E},
   {    R_A,               NOREQ,RS_NONE,           IMM|IMM_N,  TRANSFORM_NONE,TRANSFORM_NONE,                F_NONE,BIT_Z80,0x00,0xCE},
   {    R_A,               NOREQ,   RS_R,               NOREQ,  TRANSFORM_NONE,   TRANSFORM_Z,                F_NONE,BIT_Z80,0x00,0x88},
// same set, without A register
   {   R_HL,            INDIRECT,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0x00,0x8E},
   {  RS_IR,               NOREQ,RS_NONE,               NOREQ,   TRANSFORM_IR0,TRANSFORM_NONE,              F_DDFDOK,BIT_EZ80,0x00,0x8C},
   { RS_IXY,            INDIRECT,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,F_DISPA|F_DDFDOK|S_ANY,BIT_Z80,0x00,0x8E},
   {RS_NONE,           IMM|IMM_N,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                F_NONE,BIT_Z80,0x00,0xCE},
   {   RS_R,               NOREQ,RS_NONE,               NOREQ,     TRANSFORM_Z,TRANSFORM_NONE,                F_NONE,BIT_Z80,0x00,0x88},

   {   R_HL,               NOREQ,  RS_RR,               NOREQ,  TRANSFORM_NONE,   TRANSFORM_P,                 S_ANY,BIT_Z80,0xED,0x4A},
   {   R_HL,               NOREQ,   R_SP,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0xED,0x7A},
};
operandlist_t operands_add[] = {
// optimized set
   {   R_HL,               NOREQ,  RS_RR,               NOREQ,  TRANSFORM_NONE,   TRANSFORM_P,                 S_ANY,BIT_Z80,0x00,0x09},
   {    R_A,               NOREQ,   RS_R,               NOREQ,  TRANSFORM_NONE,   TRANSFORM_Z,                F_NONE,BIT_Z80,0x00,0x80},
   {    R_A,               NOREQ,   R_HL,            INDIRECT,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0x00,0x86},
// end optimized set
   {    R_A,               NOREQ,  RS_IR,               NOREQ,  TRANSFORM_NONE, TRANSFORM_IR0,              F_DDFDOK,BIT_U80|BIT_EZ80,0x00,0x84},
   {    R_A,               NOREQ, RS_IXY,            INDIRECT,  TRANSFORM_NONE,TRANSFORM_NONE,F_DISPB|F_DDFDOK|S_ANY,BIT_Z80,0x00,0x86},
   {    R_A,               NOREQ,RS_NONE,           IMM|IMM_N,  TRANSFORM_NONE,TRANSFORM_NONE,                F_NONE,BIT_Z80,0x00,0xC6},
// same set, without A register
   {   R_HL,            INDIRECT,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0x00,0x86},
   {  RS_IR,               NOREQ,RS_NONE,               NOREQ,   TRANSFORM_IR0,TRANSFORM_NONE,              F_DDFDOK,BIT_EZ80,0x00,0x84},
   { RS_IXY,            INDIRECT,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,F_DISPA|F_DDFDOK|S_ANY,BIT_Z80,0x00,0x86},
   {RS_NONE,           IMM|IMM_N,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                F_NONE,BIT_Z80,0x00,0xC6},

   {   R_HL,               NOREQ,   R_SP,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0x00,0x39},
   { RS_IXY,               NOREQ, RS_RXY,               NOREQ,  TRANSFORM_NONE,   TRANSFORM_P,        F_DDFDOK|S_ANY,BIT_Z80,0x00,0x09},
   { RS_IXY,               NOREQ,   R_SP,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,        F_DDFDOK|S_ANY,BIT_Z80,0x00,0x39},
};
operandlist_t operands_and[] = {
// optimized set
   {   RS_R,               NOREQ,RS_NONE,               NOREQ,     TRANSFORM_Z,TRANSFORM_NONE,                F_NONE,BIT_Z80,0x00,0xA0},
   {RS_NONE,           IMM|IMM_N,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                F_NONE,BIT_Z80,0x00,0xE6},
// end optimized set
   {    R_A,               NOREQ,   R_HL,            INDIRECT,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0x00,0xA6},
   {    R_A,               NOREQ,  RS_IR,               NOREQ,  TRANSFORM_NONE, TRANSFORM_IR0,              F_DDFDOK,BIT_U80|BIT_EZ80,0x00,0xA4},
   {    R_A,               NOREQ, RS_IXY,            INDIRECT,  TRANSFORM_NONE,TRANSFORM_NONE,F_DISPB|F_DDFDOK|S_ANY,BIT_Z80,0x00,0xA6},
   {    R_A,               NOREQ,RS_NONE,           IMM|IMM_N,  TRANSFORM_NONE,TRANSFORM_NONE,                F_NONE,BIT_Z80,0x00,0xE6},
   {    R_A,               NOREQ,   RS_R,               NOREQ,  TRANSFORM_NONE,   TRANSFORM_Z,                F_NONE,BIT_Z80,0x00,0xA0},
// same set, without A register
   {   R_HL,            INDIRECT,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0x00,0xA6},
   {  RS_IR,               NOREQ,RS_NONE,               NOREQ,   TRANSFORM_IR0,TRANSFORM_NONE,              F_DDFDOK,BIT_U80|BIT_EZ80,0x00,0xA4},
   { RS_IXY,            INDIRECT,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,F_DISPA|F_DDFDOK|S_ANY,BIT_Z80,0x00,0xA6},
};
operandlist_t operands_bit[] = {
   {RS_NONE,         IMM|IMM_BIT,   R_HL,            INDIRECT,     TRANSFORM_Y,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0xCB,0x46},
   {RS_NONE,         IMM|IMM_BIT, RS_IXY,            INDIRECT,     TRANSFORM_Y,TRANSFORM_NONE,F_DISPB|F_DDFDOK|S_ANY,BIT_Z80,0xCB,0x46},
   {RS_NONE,         IMM|IMM_BIT,   RS_R,               NOREQ,     TRANSFORM_Y,   TRANSFORM_Z,                F_NONE,BIT_Z80,0xCB,0x40},
};
operandlist_t operands_call[] = {
   {RS_NONE,         IMM|IMM_MMN,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0x00,0xCD},
   {RS_NONE,               NOREQ,RS_NONE,         IMM|IMM_MMN,    TRANSFORM_CC,TRANSFORM_NONE,          F_CCOK|S_ANY,BIT_Z80,0x00,0xC4},
};
operandlist_t operands_ccf[]= {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                F_NONE,BIT_Z80,0x00,0x3F},
};
operandlist_t operands_cp[]= {
// optimized set
   {RS_NONE,           IMM|IMM_N,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                F_NONE,BIT_Z80,0x00,0xFE},
   {   RS_R,               NOREQ,RS_NONE,               NOREQ,     TRANSFORM_Z,TRANSFORM_NONE,                F_NONE,BIT_Z80,0x00,0xB8},
   {   R_HL,            INDIRECT,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0x00,0xBE},
   {    R_A,               NOREQ,RS_NONE,           IMM|IMM_N,  TRANSFORM_NONE,TRANSFORM_NONE,                F_NONE,BIT_Z80,0x00,0xFE},
   {    R_A,               NOREQ,   RS_R,               NOREQ,  TRANSFORM_NONE,   TRANSFORM_Z,                F_NONE,BIT_Z80,0x00,0xB8},
   {    R_A,               NOREQ,   R_HL,            INDIRECT,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0x00,0xBE},
// end optimized set
   {    R_A,               NOREQ,  RS_IR,               NOREQ,  TRANSFORM_NONE, TRANSFORM_IR0,              F_DDFDOK,BIT_U80|BIT_EZ80,0x00,0xBC},
   {    R_A,               NOREQ, RS_IXY,            INDIRECT,  TRANSFORM_NONE,TRANSFORM_NONE,F_DISPB|F_DDFDOK|S_ANY,BIT_Z80,0x00,0xBE},
// same set, without A register
   {  RS_IR,               NOREQ,RS_NONE,               NOREQ,   TRANSFORM_IR0,TRANSFORM_NONE,              F_DDFDOK,BIT_U80|BIT_EZ80,0x00,0xBC},
   { RS_IXY,            INDIRECT,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,F_DISPA|F_DDFDOK|S_ANY,BIT_Z80,0x00,0xBE},
};
operandlist_t operands_cpd[]= {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0xED,0xA9},
};
operandlist_t operands_cpdr[]= {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0xED,0xB9},
};
operandlist_t operands_cpi[]= {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0xED,0xA1},
};
operandlist_t operands_cpir[]= {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0xED,0xB1},
};
operandlist_t operands_cpl[]= {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                F_NONE,BIT_Z80,0x00,0x2F},
};
operandlist_t operands_daa[]= {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                F_NONE,BIT_Z80,0x00,0x27},
};
operandlist_t operands_dec[]= {
// optimized set
   {  RS_RR,               NOREQ,RS_NONE,               NOREQ,     TRANSFORM_P,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0x00,0x0B},
   {   RS_R,               NOREQ,RS_NONE,               NOREQ,     TRANSFORM_Y,TRANSFORM_NONE,                F_NONE,BIT_Z80,0x00,0x05},
// end optimized set
   {   R_HL,            INDIRECT,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0x00,0x35},
   {  RS_IR,               NOREQ,RS_NONE,               NOREQ,   TRANSFORM_IR3,TRANSFORM_NONE,              F_DDFDOK,BIT_U80|BIT_EZ80,0x00,0x25},
   { RS_IXY,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,        F_DDFDOK|S_ANY,BIT_Z80,0x00,0x2B},
   { RS_IXY,            INDIRECT,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,F_DISPA|F_DDFDOK|S_ANY,BIT_Z80,0x00,0x35},
   {   R_SP,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0x00,0x3B},
};
operandlist_t operands_di[]= {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                F_NONE,BIT_Z80,0x00,0xF3},
};
operandlist_t operands_djnz[]= {
   {RS_NONE,           IMM|IMM_N,RS_NONE,               NOREQ,   TRANSFORM_REL,TRANSFORM_NONE,                F_NONE,BIT_Z80,0x00,0x10},
};
operandlist_t operands_ei[]= {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                F_NONE,BIT_Z80,0x00,0xFB},
};
operandlist_t operands_ex[]= {
// optimized set
   {   R_DE,               NOREQ,   R_HL,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                F_NONE,BIT_Z80,0x00,0xEB},
// end optimized set
   {   R_AF,               NOREQ,   R_AF,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                F_NONE,BIT_Z80,0x00,0x08},
   {   R_SP,            INDIRECT,   R_HL,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0x00,0xE3},
   {   R_SP,            INDIRECT, RS_IXY,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,        F_DDFDOK|S_ANY,BIT_Z80,0x00,0xE3},
};
operandlist_t operands_exx[]= {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                F_NONE,BIT_Z80,0x00,0xD9},
};
operandlist_t operands_halt[]= {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                F_NONE,BIT_Z80,0x00,0x76},
};
operandlist_t operands_im[]= {
   {RS_NONE,     IMM|IMM_NSELECT,RS_NONE,               NOREQ,TRANSFORM_SELECT,TRANSFORM_NONE,                F_NONE,BIT_Z80,0xED,0x46},
};
operandlist_t operands_in[]= {
   {    R_A,               NOREQ,RS_NONE,  INDIRECT|IMM|IMM_N,  TRANSFORM_NONE,TRANSFORM_NONE,                F_NONE,BIT_Z80,0x00,0xDB},
   {    R_C,            INDIRECT,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                F_NONE,BIT_U80,0xED,0x70},
   {   RS_R,               NOREQ,   R_BC,            INDIRECT,     TRANSFORM_Y,TRANSFORM_NONE,                F_NONE,BIT_Z80,0xED,0x40},
   {   RS_R,               NOREQ,    R_C,            INDIRECT,     TRANSFORM_Y,TRANSFORM_NONE,                F_NONE,BIT_Z80,0xED,0x40},
};
operandlist_t operands_in0[]= {
   {   RS_R,               NOREQ,RS_NONE,  INDIRECT|IMM|IMM_N,     TRANSFORM_Y,TRANSFORM_NONE,                F_NONE,BIT_Z180|BIT_EZ80,0xED,0x00},
};
operandlist_t operands_inc[]= {
// optimized set
   {  RS_RR,               NOREQ,RS_NONE,               NOREQ,     TRANSFORM_P,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0x00,0x03},
   {   RS_R,               NOREQ,RS_NONE,               NOREQ,     TRANSFORM_Y,TRANSFORM_NONE,                F_NONE,BIT_Z80,0x00,0x04},
   { RS_IXY,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,        F_DDFDOK|S_ANY,BIT_Z80,0x00,0x23},
   {   R_HL,            INDIRECT,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0x00,0x34},
// end optimized set
   {  RS_IR,               NOREQ,RS_NONE,               NOREQ,   TRANSFORM_IR3,TRANSFORM_NONE,              F_DDFDOK,BIT_U80|BIT_EZ80,0x00,0x24},
   { RS_IXY,            INDIRECT,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,F_DISPA|F_DDFDOK|S_ANY,BIT_Z80,0x00,0x34},
   {   R_SP,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0x00,0x33},
};
operandlist_t operands_ind[]= {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0xED,0xAA},
};
operandlist_t operands_ind2[]= {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_EZ80,0xED,0x8C},
};
operandlist_t operands_ind2r[]= {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_EZ80,0xED,0x9C},
};
operandlist_t operands_indm[]= {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_EZ80,0xED,0x8A},
};
operandlist_t operands_indmr[]= {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_EZ80,0xED,0x9A},
};
operandlist_t operands_indr[]= {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0xED,0xBA},
};
operandlist_t operands_indrx[]= {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_EZ80,0xED,0xCA},
};
operandlist_t operands_ini[]= {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0xED,0xA2},
};
operandlist_t operands_ini2[]= {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_EZ80,0xED,0x84},
};
operandlist_t operands_ini2r[]= {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_EZ80,0xED,0x94},
};
operandlist_t operands_inim[]= {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_EZ80,0xED,0x82},
};
operandlist_t operands_inimr[]= {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_EZ80,0xED,0x92},
};
operandlist_t operands_inir[]= {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0xED,0xB2},
};
operandlist_t operands_inirx[]= {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_EZ80,0xED,0xC2},
};
operandlist_t operands_jp[] = {
   {RS_NONE,                  CC,RS_NONE,         IMM|IMM_MMN,    TRANSFORM_CC,TRANSFORM_NONE,       F_CCOK|S_SISLIL,BIT_Z80,0x00,0xC2},
   {   R_HL,            INDIRECT,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0x00,0xE9},
   { RS_IXY,            INDIRECT,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,     F_DDFDOK|S_SISLIL,BIT_Z80,0x00,0xE9},
   {RS_NONE,         IMM|IMM_MMN,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,              S_SISLIL,BIT_Z80,0x00,0xC3},
};
operandlist_t operands_jr[]= {
   {RS_NONE,                 CCA,RS_NONE,           IMM|IMM_N,    TRANSFORM_CC, TRANSFORM_REL,                F_CCOK,BIT_Z80,0x00,0x20},
   {RS_NONE,           IMM|IMM_N,RS_NONE,               NOREQ,   TRANSFORM_REL,TRANSFORM_NONE,                F_NONE,BIT_Z80,0x00,0x18},
};
operandlist_t operands_ld[] = {
// start optimized set
   {  RS_RR,               NOREQ,RS_NONE,         IMM|IMM_MMN,     TRANSFORM_P,TRANSFORM_NONE,        F_DDFDOK|S_ANY,BIT_Z80,0x00,0x01},
   {   RS_R,               NOREQ,RS_NONE,           IMM|IMM_N,     TRANSFORM_Y,TRANSFORM_NONE,                F_NONE,BIT_Z80,0x00,0x06},
   {    R_A,               NOREQ,   R_HL,            INDIRECT,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0x00,0x7E},
   {RS_NONE,INDIRECT|IMM|IMM_MMN,    R_A,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0x00,0x32},
   {   RS_R,               NOREQ,   RS_R,               NOREQ,     TRANSFORM_Y,   TRANSFORM_Z,                F_NONE,BIT_Z80,0x00,0x40},
   {   R_HL,               NOREQ,RS_NONE,INDIRECT|IMM|IMM_MMN,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0x00,0x2A},
   {    R_A,               NOREQ,RS_NONE,INDIRECT|IMM|IMM_MMN,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0x00,0x3A},
   {RS_NONE,INDIRECT|IMM|IMM_MMN,   R_HL,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0x00,0x22},
   {   R_HL,            INDIRECT,   RS_R,               NOREQ,  TRANSFORM_NONE,   TRANSFORM_Z,                 S_ANY,BIT_Z80,0x00,0x70},
   {    R_A,               NOREQ,   R_DE,            INDIRECT,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0x00,0x1A},
   {  RS_RR,               NOREQ,   R_HL,            INDIRECT,     TRANSFORM_P,TRANSFORM_NONE,                 S_ANY,BIT_EZ80,0xED,0x07},
   {  RS_RR,            INDIRECT,    R_A,               NOREQ,     TRANSFORM_P,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0x00,0x02},
   { RS_IXY,               NOREQ,RS_NONE,         IMM|IMM_MMN,  TRANSFORM_NONE,TRANSFORM_NONE,        F_DDFDOK|S_ANY,BIT_Z80,0x00,0x21},
   { RS_IXY,            INDIRECT,  RS_RR,               NOREQ,  TRANSFORM_NONE,   TRANSFORM_P,F_DISPA|F_DDFDOK|S_ANY,BIT_EZ80,0x00,0x0F},
   {RS_NONE,INDIRECT|IMM|IMM_MMN, RS_IXY,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,        F_DDFDOK|S_ANY,BIT_Z80,0x00,0x22},
// end optimized set
   {    R_A,               NOREQ,    R_I,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                F_NONE,BIT_Z80,0xED,0x57},
   {    R_A,               NOREQ, RS_IXY,            INDIRECT,  TRANSFORM_NONE,TRANSFORM_NONE,F_DISPB|F_DDFDOK|S_ANY,BIT_Z80,0x00,0x7E},
   {    R_A,               NOREQ,   R_MB,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                F_NONE,BIT_EZ80,0xED,0x6E},
   {    R_A,               NOREQ,    R_R,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                F_NONE,BIT_Z80,0xED,0x5F},
   {    R_A,               NOREQ,   R_BC,            INDIRECT,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0x00,0x0A},
   {   R_HL,               NOREQ,    R_I,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                F_NONE,BIT_EZ80,0xED,0xD7},
   {   R_HL,            INDIRECT,   R_IX,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_EZ80,0xED,0x3F},
   {   R_HL,            INDIRECT,   R_IY,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_EZ80,0xED,0x3E},
   {   R_HL,            INDIRECT,RS_NONE,           IMM|IMM_N,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0x00,0x36},
   {   R_HL,            INDIRECT,  RS_RR,               NOREQ,  TRANSFORM_NONE,   TRANSFORM_P,                 S_ANY,BIT_EZ80,0xED,0x0F},
   {    R_I,               NOREQ,   R_HL,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                F_NONE,BIT_EZ80,0xED,0xC7},
   {    R_I,               NOREQ,    R_A,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                F_NONE,BIT_Z80,0xED,0x47},
   {  RS_IR,               NOREQ,  RS_IR,               NOREQ,   TRANSFORM_IR3, TRANSFORM_IR0,              F_DDFDOK,BIT_U80|BIT_EZ80,0x00,0x64},
   {  RS_IR,               NOREQ,RS_NONE,           IMM|IMM_N,   TRANSFORM_IR3,TRANSFORM_NONE,              F_DDFDOK,BIT_U80|BIT_EZ80,0x00,0x26},
   {  RS_IR,               NOREQ,  RS_AE,               NOREQ,   TRANSFORM_IR3,   TRANSFORM_Z,              F_DDFDOK,BIT_U80|BIT_EZ80,0x00,0x60},
   {   R_IX,               NOREQ,   R_HL,            INDIRECT,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_EZ80,0xED,0x37},
   {   R_IY,               NOREQ,   R_HL,            INDIRECT,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_EZ80,0xED,0x31},
   {   R_IX,               NOREQ,   R_IX,            INDIRECT,  TRANSFORM_NONE,TRANSFORM_NONE,F_DISPB|F_DDFDOK|S_ANY,BIT_EZ80,0x00,0x37},
   {   R_IY,               NOREQ,   R_IY,            INDIRECT,  TRANSFORM_NONE,TRANSFORM_NONE,F_DISPB|F_DDFDOK|S_ANY,BIT_EZ80,0x00,0x37},
   {   R_IX,               NOREQ,   R_IY,            INDIRECT,  TRANSFORM_NONE,TRANSFORM_NONE,F_DISPB|F_DDFDOK|S_ANY,BIT_EZ80,0x00,0x31},
   {   R_IY,               NOREQ,   R_IX,            INDIRECT,  TRANSFORM_NONE,TRANSFORM_NONE,F_DISPB|F_DDFDOK|S_ANY,BIT_EZ80,0x00,0x31},
   { RS_IXY,               NOREQ,RS_NONE,INDIRECT|IMM|IMM_MMN,  TRANSFORM_NONE,TRANSFORM_NONE,        F_DDFDOK|S_ANY,BIT_Z80,0x00,0x2A},
   {   R_IX,            INDIRECT,   R_IX,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,F_DISPA|F_DDFDOK|S_ANY,BIT_EZ80,0x00,0x3F},
   {   R_IY,            INDIRECT,   R_IY,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,F_DISPA|F_DDFDOK|S_ANY,BIT_EZ80,0x00,0x3F},
   {   R_IX,            INDIRECT,   R_IY,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,F_DISPA|F_DDFDOK|S_ANY,BIT_EZ80,0x00,0x3E},
   {   R_IY,            INDIRECT,   R_IX,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,F_DISPA|F_DDFDOK|S_ANY,BIT_EZ80,0x00,0x3E},
   { RS_IXY,            INDIRECT,RS_NONE,           IMM|IMM_N,  TRANSFORM_NONE,TRANSFORM_NONE,F_DISPA|F_DDFDOK|S_ANY,BIT_Z80,0x00,0x36},
   { RS_IXY,            INDIRECT,   RS_R,               NOREQ,  TRANSFORM_NONE,   TRANSFORM_Z,F_DISPA|F_DDFDOK|S_ANY,BIT_Z80,0x00,0x70},
   {   R_MB,               NOREQ,    R_A,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                F_NONE,BIT_EZ80,0xED,0x6D},
   {RS_NONE,INDIRECT|IMM|IMM_MMN,  RS_RR,               NOREQ,  TRANSFORM_NONE,   TRANSFORM_P,                 S_ANY,BIT_Z80,0xED,0x43},
   {RS_NONE,INDIRECT|IMM|IMM_MMN,   R_SP,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0xED,0x73},
   {    R_R,               NOREQ,    R_A,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                F_NONE,BIT_Z80,0xED,0x4F},
   {   RS_R,               NOREQ,   R_HL,            INDIRECT,     TRANSFORM_Y,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0x00,0x46},
   {  RS_AE,               NOREQ,  RS_IR,               NOREQ,     TRANSFORM_Y, TRANSFORM_IR0,              F_DDFDOK,BIT_U80|BIT_EZ80,0x00,0x44},
   {   RS_R,               NOREQ, RS_IXY,            INDIRECT,     TRANSFORM_Y,TRANSFORM_NONE,F_DISPB|F_DDFDOK|S_ANY,BIT_Z80,0x00,0x46},
   {  RS_RR,               NOREQ, RS_IXY,            INDIRECT,     TRANSFORM_P,TRANSFORM_NONE,F_DISPB|F_DDFDOK|S_ANY,BIT_EZ80,0x00,0x07},
   {  RS_RR,               NOREQ,RS_NONE,INDIRECT|IMM|IMM_MMN,     TRANSFORM_P,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0xED,0x4B},
   {   R_HL,            INDIRECT,    R_A,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0x00,0x77},
   {   R_SP,               NOREQ,   R_HL,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0x00,0xF9},
   {   R_SP,               NOREQ, RS_IXY,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,        F_DDFDOK|S_ANY,BIT_Z80,0x00,0xF9},
   {   R_SP,               NOREQ,RS_NONE,         IMM|IMM_MMN,  TRANSFORM_NONE,TRANSFORM_NONE,        F_DDFDOK|S_ANY,BIT_Z80,0x00,0x31},
   {   R_SP,               NOREQ,RS_NONE,INDIRECT|IMM|IMM_MMN,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0xED,0x7B},
};
operandlist_t operands_ldd[] = {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0xED,0xA8},
};
operandlist_t operands_lddr[] = {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0xED,0xB8},
};
operandlist_t operands_ldi[] = {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0xED,0xA0},
};
operandlist_t operands_ldir[] = {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0xED,0xB0},
};
operandlist_t operands_lea[] = {
// optimized set
   {  RS_RR,               NOREQ,   R_IX,               NOREQ,     TRANSFORM_P,TRANSFORM_NONE,         F_DISPB|S_ANY,BIT_EZ80,0xED,0x02},
// end optimized set
   {   R_IX,               NOREQ,   R_IX,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,         F_DISPB|S_ANY,BIT_EZ80,0xED,0x32},
   {   R_IY,               NOREQ,   R_IX,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,         F_DISPB|S_ANY,BIT_EZ80,0xED,0x55},
   {   R_IX,               NOREQ,   R_IY,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,         F_DISPB|S_ANY,BIT_EZ80,0xED,0x54},
   {   R_IY,               NOREQ,   R_IY,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,         F_DISPB|S_ANY,BIT_EZ80,0xED,0x33},
   {  RS_RR,               NOREQ,   R_IY,               NOREQ,     TRANSFORM_P,TRANSFORM_NONE,         F_DISPB|S_ANY,BIT_EZ80,0xED,0x03},
};
operandlist_t operands_mlt[] = {
   {  RS_RR,               NOREQ,RS_NONE,               NOREQ,     TRANSFORM_P,TRANSFORM_NONE,                F_NONE,BIT_Z180|BIT_EZ80,0xED,0x4C},
   {   R_SP,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z180|BIT_EZ80,0xED,0x7C},
};
operandlist_t operands_neg[] = {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                F_NONE,BIT_Z80,0xED,0x44},
};
operandlist_t operands_nop[] = {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                F_NONE,BIT_Z80,0x00,0x00},
};
operandlist_t operands_or[] = {
// optimized set
   {   RS_R,               NOREQ,RS_NONE,               NOREQ,     TRANSFORM_Z,TRANSFORM_NONE,                F_NONE,BIT_Z80,0x00,0xB0},
   {RS_NONE,           IMM|IMM_N,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                F_NONE,BIT_Z80,0x00,0xF6},
// end optimized set
   {    R_A,               NOREQ,   R_HL,            INDIRECT,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0x00,0xB6},
   {    R_A,               NOREQ,  RS_IR,               NOREQ,  TRANSFORM_NONE, TRANSFORM_IR0,              F_DDFDOK,BIT_U80|BIT_EZ80,0x00,0xB4},
   {    R_A,               NOREQ, RS_IXY,            INDIRECT,  TRANSFORM_NONE,TRANSFORM_NONE,F_DISPB|F_DDFDOK|S_ANY,BIT_Z80,0x00,0xB6},
   {    R_A,               NOREQ,RS_NONE,           IMM|IMM_N,  TRANSFORM_NONE,TRANSFORM_NONE,                F_NONE,BIT_Z80,0x00,0xF6},
   {    R_A,               NOREQ,   RS_R,               NOREQ,  TRANSFORM_NONE,   TRANSFORM_Z,                F_NONE,BIT_Z80,0x00,0xB0},
// same set, without A register
   {   R_HL,            INDIRECT,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0x00,0xB6},
   {  RS_IR,               NOREQ,RS_NONE,               NOREQ,   TRANSFORM_IR0,TRANSFORM_NONE,              F_DDFDOK,BIT_U80|BIT_EZ80,0x00,0xB4},
   { RS_IXY,            INDIRECT,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,F_DISPA|F_DDFDOK|S_ANY,BIT_Z80,0x00,0xB6},
};
operandlist_t operands_otd2r[] = {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_EZ80,0xED,0xBC},
};
operandlist_t operands_otdm[] = {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z180|BIT_EZ80,0xED,0x8B},
};
operandlist_t operands_otdmr[] = {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z180|BIT_EZ80,0xED,0x9B},
};
operandlist_t operands_otdr[] = {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0xED,0xBB},
};
operandlist_t operands_otdrx[] = {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_EZ80,0xED,0xCB},
};
operandlist_t operands_oti2r[] = {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_EZ80,0xED,0xB4},
};
operandlist_t operands_otim[] = {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z180|BIT_EZ80,0xED,0x83},
};
operandlist_t operands_otimr[] = {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z180|BIT_EZ80,0xED,0x93},
};
operandlist_t operands_otir[] = {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0xED,0xB3},
};
operandlist_t operands_otirx[] = {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_EZ80,0xED,0xC3},
};
operandlist_t operands_out[] = {
   {   R_BC,            INDIRECT,   RS_R,               NOREQ,  TRANSFORM_NONE,   TRANSFORM_Y,                F_NONE,BIT_Z80,0xED,0x41},
   {    R_C,            INDIRECT,RS_NONE,                 IMM,  TRANSFORM_NONE,TRANSFORM_NONE,                F_NONE,BIT_U80,0xED,0x71},
   {    R_C,            INDIRECT,   RS_R,               NOREQ,  TRANSFORM_NONE,   TRANSFORM_Y,                F_NONE,BIT_Z80,0xED,0x41},
   {RS_NONE,  INDIRECT|IMM|IMM_N,    R_A,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                F_NONE,BIT_Z80,0x00,0xD3},
};
operandlist_t operands_out0[] = {
   {RS_NONE,  INDIRECT|IMM|IMM_N,   RS_R,               NOREQ,  TRANSFORM_NONE,   TRANSFORM_Y,                F_NONE,BIT_Z180|BIT_EZ80,0xED,0x01},
};
operandlist_t operands_outd[] = {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0xED,0xAB},
};
operandlist_t operands_outd2[] = {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_EZ80,0xED,0xAC},
};
operandlist_t operands_outi[] = {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0xED,0xA3},
};
operandlist_t operands_outi2[] = {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_EZ80,0xED,0xA4},
};
operandlist_t operands_pea[] = {
   {   R_IX,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,         F_DISPA|S_ANY,BIT_EZ80,0xED,0x65},
   {   R_IY,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,         F_DISPA|S_ANY,BIT_EZ80,0xED,0x66},
};
operandlist_t operands_pop[] = {
   {  RS_RR,               NOREQ,RS_NONE,               NOREQ,     TRANSFORM_P,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0x00,0xC1},
   {   R_AF,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0x00,0xF1},
   { RS_IXY,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,        F_DDFDOK|S_ANY,BIT_Z80,0x00,0xE1},
};
operandlist_t operands_push[] = {
   {  RS_RR,               NOREQ,RS_NONE,               NOREQ,     TRANSFORM_P,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0x00,0xC5},
   {   R_AF,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0x00,0xF5},
   { RS_IXY,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,        F_DDFDOK|S_ANY,BIT_Z80,0x00,0xE5},
};
operandlist_t operands_res[] = {
   {RS_NONE,         IMM|IMM_BIT,   R_HL,            INDIRECT,     TRANSFORM_Y,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0xCB,0x86},
   {RS_NONE,         IMM|IMM_BIT, RS_IXY,            INDIRECT,     TRANSFORM_Y,TRANSFORM_NONE,F_DISPB|F_DDFDOK|S_ANY,BIT_Z80,0xCB,0x86},
   {RS_NONE,         IMM|IMM_BIT,   RS_R,               NOREQ,   TRANSFORM_BIT,   TRANSFORM_Z,                F_NONE,BIT_Z80,0xCB,0x80},
};
// res0 - res7 are only used for 3-operand undocumented Z80 instructions
operandlist_t operands_res0[] = {
   { RS_IXY,            INDIRECT,   RS_R,               NOREQ,     TRANSFORM_Z,   TRANSFORM_Z,      F_DISPA|F_DDFDOK,BIT_U80,0xCB,0x80},
};
operandlist_t operands_res1[] = {
   { RS_IXY,            INDIRECT,   RS_R,               NOREQ,     TRANSFORM_Z,   TRANSFORM_Z,      F_DISPA|F_DDFDOK,BIT_U80,0xCB,0x88},
};
operandlist_t operands_res2[] = {
   { RS_IXY,            INDIRECT,   RS_R,               NOREQ,     TRANSFORM_Z,   TRANSFORM_Z,      F_DISPA|F_DDFDOK,BIT_U80,0xCB,0x90},
};
operandlist_t operands_res3[] = {
   { RS_IXY,            INDIRECT,   RS_R,               NOREQ,     TRANSFORM_Z,   TRANSFORM_Z,      F_DISPA|F_DDFDOK,BIT_U80,0xCB,0x98},
};
operandlist_t operands_res4[] = {
   { RS_IXY,            INDIRECT,   RS_R,               NOREQ,     TRANSFORM_Z,   TRANSFORM_Z,      F_DISPA|F_DDFDOK,BIT_U80,0xCB,0xA0},
};
operandlist_t operands_res5[] = {
   { RS_IXY,            INDIRECT,   RS_R,               NOREQ,     TRANSFORM_Z,   TRANSFORM_Z,      F_DISPA|F_DDFDOK,BIT_U80,0xCB,0xA8},
};
operandlist_t operands_res6[] = {
   { RS_IXY,            INDIRECT,   RS_R,               NOREQ,     TRANSFORM_Z,   TRANSFORM_Z,      F_DISPA|F_DDFDOK,BIT_U80,0xCB,0xB0},
};
operandlist_t operands_res7[] = {
   { RS_IXY,            INDIRECT,   RS_R,               NOREQ,     TRANSFORM_Z,   TRANSFORM_Z,      F_DISPA|F_DDFDOK,BIT_U80,0xCB,0xB8},
};
operandlist_t operands_ret[] = {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,              S_LILLIS,BIT_Z80,0x00,0xC9},
   {RS_NONE,                  CC,RS_NONE,               NOREQ,    TRANSFORM_CC,TRANSFORM_NONE,       F_CCOK|S_LILLIS,BIT_Z80,0x00,0xC0},
};
operandlist_t operands_reti[] = {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,              S_LILLIS,BIT_Z80,0xED,0x4D},
};
operandlist_t operands_retn[] = {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,              S_LILLIS,BIT_Z80,0xED,0x45},
};
operandlist_t operands_rl[] = {
   {   R_HL,            INDIRECT,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0xCB,0x16},
   { RS_IXY,            INDIRECT,   RS_R,               NOREQ,     TRANSFORM_Z,   TRANSFORM_Z,F_DISPA|F_DDFDOK|S_ANY,BIT_U80,0xCB,0x10},
   { RS_IXY,            INDIRECT,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,F_DISPA|F_DDFDOK|S_ANY,BIT_Z80,0xCB,0x16},
   {   RS_R,               NOREQ,RS_NONE,               NOREQ,     TRANSFORM_Z,TRANSFORM_NONE,                F_NONE,BIT_Z80,0xCB,0x10},
};
operandlist_t operands_rla[] = {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                F_NONE,BIT_Z80,0x00,0x17},
};
operandlist_t operands_rlc[] = {
   {   R_HL,            INDIRECT,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0xCB,0x06},
   { RS_IXY,            INDIRECT,   RS_R,               NOREQ,     TRANSFORM_Z,   TRANSFORM_Z,F_DISPA|F_DDFDOK|S_ANY,BIT_U80,0xCB,0x00},
   { RS_IXY,            INDIRECT,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,F_DISPA|F_DDFDOK|S_ANY,BIT_Z80,0xCB,0x06},
   {   RS_R,               NOREQ,RS_NONE,               NOREQ,     TRANSFORM_Z,TRANSFORM_NONE,                F_NONE,BIT_Z80,0xCB,0x00},
};
operandlist_t operands_rlca[] = {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                F_NONE,BIT_Z80,0x00,0x07},
};
operandlist_t operands_rld[] = {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                F_NONE,BIT_Z80,0xED,0x6F},
};
operandlist_t operands_rr[] = {
   {   R_HL,            INDIRECT,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0xCB,0x1E},
   { RS_IXY,            INDIRECT,   RS_R,               NOREQ,     TRANSFORM_Z,   TRANSFORM_Z,F_DISPA|F_DDFDOK|S_ANY,BIT_U80,0xCB,0x18},
   { RS_IXY,            INDIRECT,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,F_DISPA|F_DDFDOK|S_ANY,BIT_Z80,0xCB,0x1E},
   {   RS_R,               NOREQ,RS_NONE,               NOREQ,     TRANSFORM_Z,TRANSFORM_NONE,                F_NONE,BIT_Z80,0xCB,0x18},
};
operandlist_t operands_rra[] = {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                F_NONE,BIT_Z80,0x00,0x1F},
};
operandlist_t operands_rrc[] = {
   {   R_HL,            INDIRECT,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0xCB,0x0E},
   { RS_IXY,            INDIRECT,   RS_R,               NOREQ,     TRANSFORM_Z,   TRANSFORM_Z,F_DISPA|F_DDFDOK|S_ANY,BIT_U80,0xCB,0x08},
   { RS_IXY,            INDIRECT,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,F_DISPA|F_DDFDOK|S_ANY,BIT_Z80,0xCB,0x0E},
   {   RS_R,               NOREQ,RS_NONE,               NOREQ,     TRANSFORM_Z,TRANSFORM_NONE,                F_NONE,BIT_Z80,0xCB,0x08},
};
operandlist_t operands_rrca[] = {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                F_NONE,BIT_Z80,0x00,0x0F},
};
operandlist_t operands_rrd[] = {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                F_NONE,BIT_Z80,0xED,0x67},
};
operandlist_t operands_rsmix[] = {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                F_NONE,BIT_EZ80,0xED,0x7E},
};
operandlist_t operands_rst[] = {
   {RS_NONE,           IMM|IMM_N,RS_NONE,               NOREQ,     TRANSFORM_N,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0x00,0xC7},
};
operandlist_t operands_sbc[] = {
   {    R_A,               NOREQ,   R_HL,            INDIRECT,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0x00,0x9E},
   {    R_A,               NOREQ,  RS_IR,               NOREQ,  TRANSFORM_NONE, TRANSFORM_IR0,              F_DDFDOK,BIT_U80|BIT_EZ80,0x00,0x9C},
   {    R_A,               NOREQ, RS_IXY,            INDIRECT,  TRANSFORM_NONE,TRANSFORM_NONE,F_DISPB|F_DDFDOK|S_ANY,BIT_Z80,0x00,0x9E},
   {    R_A,               NOREQ,RS_NONE,           IMM|IMM_N,  TRANSFORM_NONE,TRANSFORM_NONE,                F_NONE,BIT_Z80,0x00,0xDE},
   {    R_A,               NOREQ,   RS_R,               NOREQ,  TRANSFORM_NONE,   TRANSFORM_Z,                F_NONE,BIT_Z80,0x00,0x98},
// same set, without A register
   {   R_HL,            INDIRECT,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0x00,0x9E},
   {  RS_IR,               NOREQ,RS_NONE,               NOREQ,   TRANSFORM_IR0,TRANSFORM_NONE,              F_DDFDOK,BIT_U80|BIT_EZ80,0x00,0x9C},
   { RS_IXY,            INDIRECT,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,F_DISPA|F_DDFDOK|S_ANY,BIT_Z80,0x00,0x9E},
   {RS_NONE,           IMM|IMM_N,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                F_NONE,BIT_Z80,0x00,0xDE},
   {   RS_R,               NOREQ,RS_NONE,               NOREQ,     TRANSFORM_Z,TRANSFORM_NONE,                F_NONE,BIT_Z80,0x00,0x98},

   {   R_HL,               NOREQ,  RS_RR,               NOREQ,  TRANSFORM_NONE,   TRANSFORM_P,                 S_ANY,BIT_Z80,0xED,0x42},
   {   R_HL,               NOREQ,   R_SP,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0xED,0x72},
};
operandlist_t operands_scf[] = {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                F_NONE,BIT_Z80,0x00,0x37},
};
operandlist_t operands_set[] = {
   {RS_NONE,         IMM|IMM_BIT,   R_HL,            INDIRECT,     TRANSFORM_Y,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0xCB,0xC6},
   {RS_NONE,         IMM|IMM_BIT, RS_IXY,            INDIRECT,     TRANSFORM_Y,TRANSFORM_NONE,F_DISPB|F_DDFDOK|S_ANY,BIT_Z80,0xCB,0xC6},
   {RS_NONE,         IMM|IMM_BIT,   RS_R,               NOREQ,   TRANSFORM_BIT,   TRANSFORM_Z,                F_NONE,BIT_Z80,0xCB,0xC0},
};
// set0 - set7 are only used for 3-operand undocumented Z80 instructions
operandlist_t operands_set0[] = {
   { RS_IXY,            INDIRECT,   RS_R,               NOREQ,     TRANSFORM_Z,   TRANSFORM_Z,      F_DISPA|F_DDFDOK,BIT_U80,0xCB,0xC0},
};
operandlist_t operands_set1[] = {
   { RS_IXY,            INDIRECT,   RS_R,               NOREQ,     TRANSFORM_Z,   TRANSFORM_Z,      F_DISPA|F_DDFDOK,BIT_U80,0xCB,0xC8},
};
operandlist_t operands_set2[] = {
   { RS_IXY,            INDIRECT,   RS_R,               NOREQ,     TRANSFORM_Z,   TRANSFORM_Z,      F_DISPA|F_DDFDOK,BIT_U80,0xCB,0xD0},
};
operandlist_t operands_set3[] = {
   { RS_IXY,            INDIRECT,   RS_R,               NOREQ,     TRANSFORM_Z,   TRANSFORM_Z,      F_DISPA|F_DDFDOK,BIT_U80,0xCB,0xD8},
};
operandlist_t operands_set4[] = {
   { RS_IXY,            INDIRECT,   RS_R,               NOREQ,     TRANSFORM_Z,   TRANSFORM_Z,      F_DISPA|F_DDFDOK,BIT_U80,0xCB,0xE0},
};
operandlist_t operands_set5[] = {
   { RS_IXY,            INDIRECT,   RS_R,               NOREQ,     TRANSFORM_Z,   TRANSFORM_Z,      F_DISPA|F_DDFDOK,BIT_U80,0xCB,0xE8},
};
operandlist_t operands_set6[] = {
   { RS_IXY,            INDIRECT,   RS_R,               NOREQ,     TRANSFORM_Z,   TRANSFORM_Z,      F_DISPA|F_DDFDOK,BIT_U80,0xCB,0xF0},
};
operandlist_t operands_set7[] = {
   { RS_IXY,            INDIRECT,   RS_R,               NOREQ,     TRANSFORM_Z,   TRANSFORM_Z,      F_DISPA|F_DDFDOK,BIT_U80,0xCB,0xF8},
};
operandlist_t operands_sla[] = {
   {   R_HL,            INDIRECT,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0xCB,0x26},
   { RS_IXY,            INDIRECT,   RS_R,               NOREQ,     TRANSFORM_Z,   TRANSFORM_Z,F_DISPA|F_DDFDOK|S_ANY,BIT_U80,0xCB,0x20},
   { RS_IXY,            INDIRECT,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,F_DISPA|F_DDFDOK|S_ANY,BIT_Z80,0xCB,0x26},
   {   RS_R,               NOREQ,RS_NONE,               NOREQ,     TRANSFORM_Z,TRANSFORM_NONE,                F_NONE,BIT_Z80,0xCB,0x20},
};
operandlist_t operands_sll[] = {
   {   RS_R,               NOREQ,RS_NONE,               NOREQ,     TRANSFORM_Z,TRANSFORM_NONE,                F_NONE,BIT_U80,0xCB,0x30},
   { RS_IXY,            INDIRECT,   RS_R,               NOREQ,     TRANSFORM_Z,   TRANSFORM_Z,F_DISPA|F_DDFDOK|S_ANY,BIT_U80,0xCB,0x30},
   { RS_IXY,            INDIRECT,RS_NONE,               NOREQ,     TRANSFORM_Z,TRANSFORM_NONE,F_DISPA|F_DDFDOK|S_ANY,BIT_U80,0xCB,0x36},
   {   R_HL,            INDIRECT,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                F_NONE,BIT_Z80,0xCB,0x36},
};
operandlist_t operands_slp[] = {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                F_NONE,BIT_Z180|BIT_EZ80,0xED,0x76},
};
operandlist_t operands_sra[] = {
   {   R_HL,            INDIRECT,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0xCB,0x2E},
   { RS_IXY,            INDIRECT,   RS_R,               NOREQ,     TRANSFORM_Z,   TRANSFORM_Z,F_DISPA|F_DDFDOK|S_ANY,BIT_U80,0xCB,0x28},
   { RS_IXY,            INDIRECT,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,F_DISPA|F_DDFDOK|S_ANY,BIT_Z80,0xCB,0x2E},
   {   RS_R,               NOREQ,RS_NONE,               NOREQ,     TRANSFORM_Z,TRANSFORM_NONE,                F_NONE,BIT_Z80,0xCB,0x28},
};
operandlist_t operands_srl[] = {
   {   R_HL,            INDIRECT,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0xCB,0x3E},
   { RS_IXY,            INDIRECT,   RS_R,               NOREQ,     TRANSFORM_Z,   TRANSFORM_Z,F_DISPA|F_DDFDOK|S_ANY,BIT_U80,0xCB,0x38},
   { RS_IXY,            INDIRECT,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,F_DISPA|F_DDFDOK|S_ANY,BIT_Z80,0xCB,0x3E},
   {   RS_R,               NOREQ,RS_NONE,               NOREQ,     TRANSFORM_Z,TRANSFORM_NONE,                F_NONE,BIT_Z80,0xCB,0x38},
};
operandlist_t operands_stmix[] = {
   {RS_NONE,               NOREQ,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                F_NONE,BIT_EZ80,0xED,0x7D},
};
operandlist_t operands_sub[] = {
// optimized set
   {   R_HL,            INDIRECT,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0x00,0x96},
// end optimized set
   {    R_A,               NOREQ,   R_HL,            INDIRECT,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0x00,0x96},
   {    R_A,               NOREQ,  RS_IR,               NOREQ,  TRANSFORM_NONE, TRANSFORM_IR0,              F_DDFDOK,BIT_U80|BIT_EZ80,0x00,0x94},
   {    R_A,               NOREQ, RS_IXY,            INDIRECT,  TRANSFORM_NONE,TRANSFORM_NONE,F_DISPB|F_DDFDOK|S_ANY,BIT_Z80,0x00,0x96},
   {    R_A,               NOREQ,RS_NONE,           IMM|IMM_N,  TRANSFORM_NONE,TRANSFORM_NONE,                F_NONE,BIT_Z80,0x00,0xD6},
   {    R_A,               NOREQ,   RS_R,               NOREQ,  TRANSFORM_NONE,   TRANSFORM_Z,                F_NONE,BIT_Z80,0x00,0x90},
// same set, without A register
   {  RS_IR,               NOREQ,RS_NONE,               NOREQ,   TRANSFORM_IR0,TRANSFORM_NONE,              F_DDFDOK,BIT_U80|BIT_EZ80,0x00,0x94},
   { RS_IXY,            INDIRECT,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,F_DISPA|F_DDFDOK|S_ANY,BIT_Z80,0x00,0x96},
   {RS_NONE,           IMM|IMM_N,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                F_NONE,BIT_Z80,0x00,0xD6},
   {   RS_R,               NOREQ,RS_NONE,               NOREQ,     TRANSFORM_Z,TRANSFORM_NONE,                F_NONE,BIT_Z80,0x00,0x90},
};
operandlist_t operands_tst[] = {
   {    R_A,               NOREQ,   R_HL,            INDIRECT,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z180|BIT_EZ80,0xED,0x34},
   {    R_A,               NOREQ,RS_NONE,           IMM|IMM_N,  TRANSFORM_NONE,TRANSFORM_NONE,                F_NONE,BIT_Z180|BIT_EZ80,0xED,0x64},
   {    R_A,               NOREQ,   RS_R,               NOREQ,  TRANSFORM_NONE,   TRANSFORM_Y,                F_NONE,BIT_Z180|BIT_EZ80,0xED,0x04},
// same set, without A register
   {   R_HL,            INDIRECT,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z180|BIT_EZ80,0xED,0x34},
   {RS_NONE,           IMM|IMM_N,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                F_NONE,BIT_Z180|BIT_EZ80,0xED,0x64},
   {   RS_R,               NOREQ,RS_NONE,               NOREQ,     TRANSFORM_Y,TRANSFORM_NONE,                F_NONE,BIT_Z180|BIT_EZ80,0xED,0x04},
};
operandlist_t operands_tstio[] = {
   {RS_NONE,           IMM|IMM_N,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                F_NONE,BIT_Z180|BIT_EZ80,0xED,0x74},
};
operandlist_t operands_xor[] = {
// optimized set
   {   RS_R,               NOREQ,RS_NONE,               NOREQ,     TRANSFORM_Z,TRANSFORM_NONE,                F_NONE,BIT_Z80,0x00,0xA8},
   {RS_NONE,           IMM|IMM_N,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                F_NONE,BIT_Z80,0x00,0xEE},
   {   R_HL,            INDIRECT,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0x00,0xAE},
// end optimized set
   {    R_A,               NOREQ,   R_HL,            INDIRECT,  TRANSFORM_NONE,TRANSFORM_NONE,                 S_ANY,BIT_Z80,0x00,0xAE},
   {    R_A,               NOREQ,  RS_IR,               NOREQ,  TRANSFORM_NONE, TRANSFORM_IR0,              F_DDFDOK,BIT_U80|BIT_EZ80,0x00,0xAC},
   {    R_A,               NOREQ, RS_IXY,            INDIRECT,  TRANSFORM_NONE,TRANSFORM_NONE,F_DISPB|F_DDFDOK|S_ANY,BIT_Z80,0x00,0xAE},
   {    R_A,               NOREQ,RS_NONE,           IMM|IMM_N,  TRANSFORM_NONE,TRANSFORM_NONE,                F_NONE,BIT_Z80,0x00,0xEE},
   {    R_A,               NOREQ,   RS_R,               NOREQ,  TRANSFORM_NONE,   TRANSFORM_Z,                F_NONE,BIT_Z80,0x00,0xA8},
// same set, without A register
   {  RS_IR,               NOREQ,RS_NONE,               NOREQ,   TRANSFORM_IR0,TRANSFORM_NONE,              F_DDFDOK,BIT_U80|BIT_EZ80,0x00,0xAC},
   { RS_IXY,            INDIRECT,RS_NONE,               NOREQ,  TRANSFORM_NONE,TRANSFORM_NONE,F_DISPA|F_DDFDOK|S_ANY,BIT_Z80,0x00,0xAE},
};

instruction_t instructions[] = {
    {"adc",         EZ80, 0, sizeof(operands_adc)/sizeof(operandlist_t), operands_adc,NULL,NULL},
    {"add",         EZ80, 0, sizeof(operands_add)/sizeof(operandlist_t), operands_add,NULL,NULL},
    {"align",       ASSEMBLER, ASM_ALIGN, 0, NULL,NULL,NULL},
    {"and",         EZ80, 0, sizeof(operands_and)/sizeof(operandlist_t), operands_and,NULL,NULL},
    {"ascii",       ASSEMBLER, ASM_DB, 0, NULL,NULL,NULL},
    {"asciz",       ASSEMBLER, ASM_ASCIZ, 0, NULL,NULL,NULL},
    {"assume",      ASSEMBLER, ASM_ADL, 0, NULL,NULL,NULL},
    {"bit",         EZ80, 0, sizeof(operands_bit)/sizeof(operandlist_t), operands_bit,NULL,NULL},
    {"blkb",        ASSEMBLER, ASM_BLKB, 0, NULL,NULL,NULL},
    {"blkl",        ASSEMBLER, ASM_BLKL, 0, NULL,NULL,NULL},
    {"blkp",        ASSEMBLER, ASM_BLKP, 0, NULL,NULL,NULL},
    {"blkw",        ASSEMBLER, ASM_BLKW, 0, NULL,NULL,NULL},
    {"byte",        ASSEMBLER, ASM_DB, 0, NULL,NULL,NULL},
    {"call",        EZ80, 0, sizeof(operands_call)/sizeof(operandlist_t), operands_call,NULL,NULL},
    {"ccf",         EZ80, 0, sizeof(operands_ccf)/sizeof(operandlist_t), operands_ccf,NULL,NULL},
    {"cp",          EZ80, 0, sizeof(operands_cp)/sizeof(operandlist_t), operands_cp,NULL,NULL},
    {"cpd",         EZ80, 0, sizeof(operands_cpd)/sizeof(operandlist_t), operands_cpd,NULL,NULL},
    {"cpdr",        EZ80, 0, sizeof(operands_cpdr)/sizeof(operandlist_t), operands_cpdr,NULL,NULL},
    {"cpi",         EZ80, 0, sizeof(operands_cpi)/sizeof(operandlist_t), operands_cpi,NULL,NULL},
    {"cpir",        EZ80, 0, sizeof(operands_cpir)/sizeof(operandlist_t), operands_cpir,NULL,NULL},
    {"cpl",         EZ80, 0, sizeof(operands_cpl)/sizeof(operandlist_t), operands_cpl,NULL,NULL},
    {"cpu",         ASSEMBLER, ASM_CPU, 0, NULL,NULL,NULL},
    {"daa",         EZ80, 0, sizeof(operands_daa)/sizeof(operandlist_t), operands_daa,NULL,NULL},
    {"db",          ASSEMBLER, ASM_DB, 0, NULL,NULL,NULL},
    {"dec",         EZ80, 0, sizeof(operands_dec)/sizeof(operandlist_t), operands_dec,NULL,NULL},
    {"defb",        ASSEMBLER, ASM_DB, 0, NULL,NULL,NULL},
    {"defs",        ASSEMBLER, ASM_DS, 0, NULL,NULL,NULL},
    {"defw",        ASSEMBLER, ASM_DW, 0, NULL,NULL,NULL},
    {"di",          EZ80, 0, sizeof(operands_di)/sizeof(operandlist_t), operands_di,NULL,NULL},
    {"djnz",        EZ80, 0, sizeof(operands_djnz)/sizeof(operandlist_t), operands_djnz,NULL,NULL},
    {"dl",          ASSEMBLER, ASM_DW24, 0, NULL,NULL,NULL},
    {"ds",          ASSEMBLER, ASM_DS, 0, NULL,NULL,NULL},
    {"dw",          ASSEMBLER, ASM_DW, 0, NULL,NULL,NULL},
    {"dw24",        ASSEMBLER, ASM_DW24, 0, NULL,NULL,NULL},
    {"dw32",        ASSEMBLER, ASM_DW32, 0, NULL,NULL,NULL},
    {"ei",          EZ80, 0, sizeof(operands_ei)/sizeof(operandlist_t), operands_ei,NULL,NULL},
    {"else",        ASSEMBLER, ASM_ELSE, 0, NULL,NULL,NULL},
    {"endif",       ASSEMBLER, ASM_ENDIF, 0, NULL,NULL,NULL},
    {"endmacro",    ASSEMBLER, ASM_MACRO_END, 0, NULL,NULL,NULL},
    {"endrelocate", ASSEMBLER, ASM_ENDRELOCATE, 0, NULL,NULL,NULL},
    {"equ",         ASSEMBLER, ASM_EQU, 0, NULL,NULL,NULL},
    {"ex",          EZ80, 0, sizeof(operands_ex)/sizeof(operandlist_t), operands_ex,NULL,NULL},
    {"exx",         EZ80, 0, sizeof(operands_exx)/sizeof(operandlist_t), operands_exx,NULL,NULL},
    {"fillbyte",    ASSEMBLER, ASM_FILLBYTE, 0, NULL,NULL,NULL},
    {"halt",        EZ80, 0, sizeof(operands_halt)/sizeof(operandlist_t), operands_halt,NULL,NULL},
    {"if",          ASSEMBLER, ASM_IF, 0, NULL,NULL,NULL},
    {"im",          EZ80, 0, sizeof(operands_im)/sizeof(operandlist_t), operands_im,NULL,NULL},
    {"in",          EZ80, 0, sizeof(operands_in)/sizeof(operandlist_t), operands_in,NULL,NULL},
    {"in0",         EZ80, 0, sizeof(operands_in0)/sizeof(operandlist_t), operands_in0,NULL,NULL},
    {"inc",         EZ80, 0, sizeof(operands_inc)/sizeof(operandlist_t), operands_inc,NULL,NULL},
    {"incbin",      ASSEMBLER, ASM_INCBIN, 0, NULL,NULL,NULL},
    {"include",     ASSEMBLER, ASM_INCLUDE, 0, NULL,NULL,NULL},
    {"ind",         EZ80, 0, sizeof(operands_ind)/sizeof(operandlist_t), operands_ind,NULL,NULL},
    {"ind2",        EZ80, 0, sizeof(operands_ind2)/sizeof(operandlist_t), operands_ind2,NULL,NULL},
    {"ind2r",       EZ80, 0, sizeof(operands_ind2r)/sizeof(operandlist_t), operands_ind2r,NULL,NULL},
    {"indm",        EZ80, 0, sizeof(operands_indm)/sizeof(operandlist_t), operands_indm,NULL,NULL},
    {"indmr",       EZ80, 0, sizeof(operands_indmr)/sizeof(operandlist_t), operands_indmr,NULL,NULL},
    {"indr",        EZ80, 0, sizeof(operands_indr)/sizeof(operandlist_t), operands_indr,NULL,NULL},
    {"indrx",       EZ80, 0, sizeof(operands_indrx)/sizeof(operandlist_t), operands_indrx,NULL,NULL},
    {"ini",         EZ80, 0, sizeof(operands_ini)/sizeof(operandlist_t), operands_ini,NULL,NULL},
    {"ini2",        EZ80, 0, sizeof(operands_ini2)/sizeof(operandlist_t), operands_ini2,NULL,NULL},
    {"ini2r",       EZ80, 0, sizeof(operands_ini2r)/sizeof(operandlist_t), operands_ini2r,NULL,NULL},
    {"inim",        EZ80, 0, sizeof(operands_inim)/sizeof(operandlist_t), operands_inim,NULL,NULL},
    {"inimr",       EZ80, 0, sizeof(operands_inimr)/sizeof(operandlist_t), operands_inimr,NULL,NULL},
    {"inir",        EZ80, 0, sizeof(operands_inir)/sizeof(operandlist_t), operands_inir,NULL,NULL},
    {"inirx",       EZ80, 0, sizeof(operands_inirx)/sizeof(operandlist_t), operands_inirx,NULL,NULL},
    {"jp",          EZ80, 0, sizeof(operands_jp)/sizeof(operandlist_t), operands_jp,NULL,NULL},
    {"jr",          EZ80, 0, sizeof(operands_jr)/sizeof(operandlist_t), operands_jr,NULL,NULL},
    {"ld",          EZ80, 0, sizeof(operands_ld)/sizeof(operandlist_t), operands_ld,NULL,NULL},
    {"ldd",         EZ80, 0, sizeof(operands_ldd)/sizeof(operandlist_t), operands_ldd,NULL,NULL},
    {"lddr",        EZ80, 0, sizeof(operands_lddr)/sizeof(operandlist_t), operands_lddr,NULL,NULL},
    {"ldi",         EZ80, 0, sizeof(operands_ldi)/sizeof(operandlist_t), operands_ldi,NULL,NULL},
    {"ldir",        EZ80, 0, sizeof(operands_ldir)/sizeof(operandlist_t), operands_ldir,NULL,NULL},
    {"lea",         EZ80, 0, sizeof(operands_lea)/sizeof(operandlist_t), operands_lea,NULL,NULL},
    {"macro",       ASSEMBLER, ASM_MACRO_START, 0, NULL,NULL,NULL},
    {"mlt",         EZ80, 0, sizeof(operands_mlt)/sizeof(operandlist_t), operands_mlt,NULL,NULL},
    {"neg",         EZ80, 0, sizeof(operands_neg)/sizeof(operandlist_t), operands_neg,NULL,NULL},
    {"nop",         EZ80, 0, sizeof(operands_nop)/sizeof(operandlist_t), operands_nop,NULL,NULL},
    {"or",          EZ80, 0, sizeof(operands_or)/sizeof(operandlist_t), operands_or,NULL,NULL},
    {"org",         ASSEMBLER, ASM_ORG, 0, NULL,NULL,NULL},
    {"otd2r",       EZ80, 0, sizeof(operands_otd2r)/sizeof(operandlist_t), operands_otd2r,NULL,NULL},
    {"otdm",        EZ80, 0, sizeof(operands_otdm)/sizeof(operandlist_t), operands_otdm,NULL,NULL},
    {"otdmr",       EZ80, 0, sizeof(operands_otdmr)/sizeof(operandlist_t), operands_otdmr,NULL,NULL},
    {"otdr",        EZ80, 0, sizeof(operands_otdr)/sizeof(operandlist_t), operands_otdr,NULL,NULL},
    {"otdrx",       EZ80, 0, sizeof(operands_otdrx)/sizeof(operandlist_t), operands_otdrx,NULL,NULL},
    {"oti2r",       EZ80, 0, sizeof(operands_oti2r)/sizeof(operandlist_t), operands_oti2r,NULL,NULL},
    {"otim",        EZ80, 0, sizeof(operands_otim)/sizeof(operandlist_t), operands_otim,NULL,NULL},
    {"otimr",       EZ80, 0, sizeof(operands_otimr)/sizeof(operandlist_t), operands_otimr,NULL,NULL},
    {"otir",        EZ80, 0, sizeof(operands_otir)/sizeof(operandlist_t), operands_otir,NULL,NULL},
    {"otirx",       EZ80, 0, sizeof(operands_otirx)/sizeof(operandlist_t), operands_otirx,NULL,NULL},
    {"out",         EZ80, 0, sizeof(operands_out)/sizeof(operandlist_t), operands_out,NULL,NULL},
    {"out0",        EZ80, 0, sizeof(operands_out0)/sizeof(operandlist_t), operands_out0,NULL,NULL},
    {"outd",        EZ80, 0, sizeof(operands_outd)/sizeof(operandlist_t), operands_outd,NULL,NULL},
    {"outd2",       EZ80, 0, sizeof(operands_outd2)/sizeof(operandlist_t), operands_outd2,NULL,NULL},
    {"outi",        EZ80, 0, sizeof(operands_outi)/sizeof(operandlist_t), operands_outi,NULL,NULL},
    {"outi2",       EZ80, 0, sizeof(operands_outi2)/sizeof(operandlist_t), operands_outi2,NULL,NULL},
    {"pea",         EZ80, 0, sizeof(operands_pea)/sizeof(operandlist_t), operands_pea,NULL,NULL},
    {"pop",         EZ80, 0, sizeof(operands_pop)/sizeof(operandlist_t), operands_pop,NULL,NULL},
    {"push",        EZ80, 0, sizeof(operands_push)/sizeof(operandlist_t), operands_push,NULL,NULL},
    {"relocate",    ASSEMBLER, ASM_RELOCATE, 0, NULL,NULL,NULL},
    {"res",         EZ80, 0, sizeof(operands_res)/sizeof(operandlist_t), operands_res,NULL,NULL},
    {"res0",        EZ80, 0, sizeof(operands_res0)/sizeof(operandlist_t), operands_res0,NULL,NULL},
    {"res1",        EZ80, 0, sizeof(operands_res1)/sizeof(operandlist_t), operands_res1,NULL,NULL},
    {"res2",        EZ80, 0, sizeof(operands_res2)/sizeof(operandlist_t), operands_res2,NULL,NULL},
    {"res3",        EZ80, 0, sizeof(operands_res3)/sizeof(operandlist_t), operands_res3,NULL,NULL},
    {"res4",        EZ80, 0, sizeof(operands_res4)/sizeof(operandlist_t), operands_res4,NULL,NULL},
    {"res5",        EZ80, 0, sizeof(operands_res5)/sizeof(operandlist_t), operands_res5,NULL,NULL},
    {"res6",        EZ80, 0, sizeof(operands_res6)/sizeof(operandlist_t), operands_res6,NULL,NULL},
    {"res7",        EZ80, 0, sizeof(operands_res7)/sizeof(operandlist_t), operands_res7,NULL,NULL},
    {"ret",         EZ80, 0, sizeof(operands_ret)/sizeof(operandlist_t), operands_ret,NULL,NULL},
    {"reti",        EZ80, 0, sizeof(operands_reti)/sizeof(operandlist_t), operands_reti,NULL,NULL},
    {"retn",        EZ80, 0, sizeof(operands_retn)/sizeof(operandlist_t), operands_retn,NULL,NULL},
    {"rl",          EZ80, 0, sizeof(operands_rl)/sizeof(operandlist_t), operands_rl,NULL,NULL},
    {"rla",         EZ80, 0, sizeof(operands_rla)/sizeof(operandlist_t), operands_rla,NULL,NULL},
    {"rlc",         EZ80, 0, sizeof(operands_rlc)/sizeof(operandlist_t), operands_rlc,NULL,NULL},
    {"rlca",        EZ80, 0, sizeof(operands_rlca)/sizeof(operandlist_t), operands_rlca,NULL,NULL},
    {"rld",         EZ80, 0, sizeof(operands_rld)/sizeof(operandlist_t), operands_rld,NULL,NULL},
    {"rr",          EZ80, 0, sizeof(operands_rr)/sizeof(operandlist_t), operands_rr,NULL,NULL},
    {"rra",         EZ80, 0, sizeof(operands_rra)/sizeof(operandlist_t), operands_rra,NULL,NULL},
    {"rrc",         EZ80, 0, sizeof(operands_rrc)/sizeof(operandlist_t), operands_rrc,NULL,NULL},
    {"rrca",        EZ80, 0, sizeof(operands_rrca)/sizeof(operandlist_t), operands_rrca,NULL,NULL},
    {"rrd",         EZ80, 0, sizeof(operands_rrd)/sizeof(operandlist_t), operands_rrd,NULL,NULL},
    {"rsmix",       EZ80, 0, sizeof(operands_rsmix)/sizeof(operandlist_t), operands_rsmix,NULL,NULL},
    {"rst",         EZ80, 0, sizeof(operands_rst)/sizeof(operandlist_t), operands_rst,NULL,NULL},
    {"sbc",         EZ80, 0, sizeof(operands_sbc)/sizeof(operandlist_t), operands_sbc,NULL,NULL},
    {"scf",         EZ80, 0, sizeof(operands_scf)/sizeof(operandlist_t), operands_scf,NULL,NULL},
    {"set",         EZ80, 0, sizeof(operands_set)/sizeof(operandlist_t), operands_set,NULL,NULL},
    {"set0",        EZ80, 0, sizeof(operands_set0)/sizeof(operandlist_t), operands_set0,NULL,NULL},
    {"set1",        EZ80, 0, sizeof(operands_set1)/sizeof(operandlist_t), operands_set1,NULL,NULL},
    {"set2",        EZ80, 0, sizeof(operands_set2)/sizeof(operandlist_t), operands_set2,NULL,NULL},
    {"set3",        EZ80, 0, sizeof(operands_set3)/sizeof(operandlist_t), operands_set3,NULL,NULL},
    {"set4",        EZ80, 0, sizeof(operands_set4)/sizeof(operandlist_t), operands_set4,NULL,NULL},
    {"set5",        EZ80, 0, sizeof(operands_set5)/sizeof(operandlist_t), operands_set5,NULL,NULL},
    {"set6",        EZ80, 0, sizeof(operands_set6)/sizeof(operandlist_t), operands_set6,NULL,NULL},
    {"set7",        EZ80, 0, sizeof(operands_set7)/sizeof(operandlist_t), operands_set7,NULL,NULL},
    {"sla",         EZ80, 0, sizeof(operands_sla)/sizeof(operandlist_t), operands_sla,NULL,NULL},
    {"sll",         EZ80, 0, sizeof(operands_sll)/sizeof(operandlist_t), operands_sll,NULL,NULL},
    {"slp",         EZ80, 0, sizeof(operands_slp)/sizeof(operandlist_t), operands_slp,NULL,NULL},
    {"sra",         EZ80, 0, sizeof(operands_sra)/sizeof(operandlist_t), operands_sra,NULL,NULL},
    {"srl",         EZ80, 0, sizeof(operands_srl)/sizeof(operandlist_t), operands_srl,NULL,NULL},
    {"stmix",       EZ80, 0, sizeof(operands_stmix)/sizeof(operandlist_t), operands_stmix,NULL,NULL},
    {"sub",         EZ80, 0, sizeof(operands_sub)/sizeof(operandlist_t), operands_sub,NULL,NULL},
    {"tst",         EZ80, 0, sizeof(operands_tst)/sizeof(operandlist_t), operands_tst,NULL,NULL},
    {"tstio",       EZ80, 0, sizeof(operands_tstio)/sizeof(operandlist_t), operands_tstio,NULL,NULL},
    {"xor",         EZ80, 0, sizeof(operands_xor)/sizeof(operandlist_t), operands_xor,NULL,NULL}
};

instruction_t * instruction_lookup(const char *name) {
    uint8_t index;
    instruction_t *try;

    index = lowercaseHash256(name);
    try = instruction_table[index];

    while(true)
    {
        if(try == NULL) return NULL;
        if(fast_strcasecmp(try->name, name) == 0) return try;
        try = try->next;
    }
}

void initInstructionTable(void) {
    uint16_t n;
    uint8_t index;
    instruction_t *try;

    memset(instruction_table, 0, sizeof(instruction_table));

    for(n = 0; n < (sizeof(instructions) / sizeof(instruction_t)); n++) {
      index = lowercaseHash256(instructions[n].name);
      try = instruction_table[index];

      // First item on index
      if(try == NULL) {
         instruction_table[index] = &instructions[n];
      }
      else {
         while(true) {
            if(try->next) {
                  try = try->next;
            }
            else {
                  try->next = &instructions[n];
                  break;
            }
         }
      }
   }
}
