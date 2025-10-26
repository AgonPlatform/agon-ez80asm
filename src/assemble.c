#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "config.h"
#include "defines.h"
#include "globals.h"
#include "instruction.h"
#include "utils.h"
#include "label.h"
#include "listing.h"
#include "macro.h"
#include "io.h"
#include "moscalls.h"
#include "hash.h"
#include "str2num.h"
#include "assemble.h"

// linebuffer for replacement arguments during macro expansion
char macro_expansionbuffer[MACROLINEMAX + 1];

contentitem_t *findContent(const char *filename) {
    uint8_t index;
    contentitem_t *try;

    index = hash256(filename);
    try = filecontent[index];

    while(true)
    {
        if(try == NULL) return NULL;
        if(strcmp(try->name, filename) == 0) return try;
        try = try->next;
    }
}

contentitem_t *insertContent(const char *filename) {
    contentitem_t *ci, *try;
    uint8_t index;

    // Allocate memory and fill out ci content
    ci = allocateMemory(sizeof(contentitem_t), &filecontentsize);
    if(ci == NULL) return NULL;
    ci->name = allocateString(filename, &filecontentsize);
    if(ci->name == NULL) return NULL;

    if(completefilebuffering) {
        ci->fh = ioOpenfile(filename, "rb");
        if(ci->fh == 0) return NULL;
        ci->size = ioGetfilesize(ci->fh);
        ci->buffer = allocateMemory(ci->size+1, &filecontentsize);
        if(ci->buffer == NULL) return NULL;
        if(fread(ci->buffer, 1, ci->size, ci->fh) != ci->size) {
            error(message[ERROR_READINGINPUT],0);
            return NULL;
        }
        ci->buffer[ci->size] = 0; // terminate stringbuffer
        fclose(ci->fh);
    }
    strcpy(ci->labelscope, ""); // empty scope
    ci->next = NULL;

    // Placement
    index = hash256(filename);
    try = filecontent[index];
    // First item on index
    if(try == NULL) {
        filecontent[index] = ci;
        return ci;
    }

    // Collision on index, place at end of linked list. Items are always unique
    while(true) {
        if(try->next) {
            try = try->next;
        }
        else {
            try->next = ci;
            return ci;
        }
    }
}

// Parse a command-token string to currentline.mnemonic & currentline.suffix
void parse_command(char *src) {
    currentline.mnemonic = src;

    while(*src && (*src != '.')) src++;
    if(*src) {
        // suffix start found
        *src = 0; // terminate mnemonic
        currentline.suffixpresent = true;
        currentline.suffix = src + 1;
        return;
    }
    // no suffix found
    currentline.suffixpresent = false;
    currentline.suffix = NULL;
    return;
}

// parses the given string to the operand, or throws errors along the way
// will destruct parts of the original string during the process
void parse_operand(char *string, uint8_t len, operand_t *operand) {
    char *ptr = string;

    operand->addressmode = NOREQ;
    operand->reg = R_NONE;

    // direct or indirect
    if(*ptr == '(') {
        operand->indirect = true;
        operand->addressmode |= INDIRECT;
        // find closing bracket or error out
        if(string[len-1] == ')') string[len-1] = 0; // terminate on closing bracket
        else error(message[ERROR_CLOSINGBRACKET],0);
        ptr = &string[1];
        while(isspace(*ptr)) ptr++; // eat spaces
    }
    else {
        operand->indirect = false;
        // should not find a closing bracket
        if(string[len-1] == ')') error(message[ERROR_OPENINGBRACKET],0);
    }
    
    switch(*ptr++) {
        case 0: // empty operand
            break;
        case 'a':
        case 'A':
            switch(*ptr++) {
                case 0:
                    operand->reg = R_A;
                    operand->reg_index = R_INDEX_A;
                    return;
                case 'f':
                case 'F':
                    switch(*ptr++) {
                        case 0:
                        case '\'':
                            operand->reg = R_AF;
                            operand->reg_index = R_INDEX_AF;
                            return;
                        default:
                            break;
                    }
                    break;
                default:
                    break;
            }
            break;
        case 'b':
        case 'B':
            switch(*ptr++) {
                case 0:
                    operand->reg = R_B;
                    operand->reg_index = R_INDEX_B;
                    return;
                case 'c':
                case 'C':
                    if((*ptr == 0) || isspace(*ptr)) {
                        operand->reg = R_BC;
                        operand->reg_index = R_INDEX_BC;
                        return;
                    }
                    break;
                default:
                    break;
            }
            break;
        case 'c':
        case 'C':
            switch(*ptr++) {
                case 0:
                    operand->reg = R_C;
                    operand->reg_index = R_INDEX_C;
                    operand->cc = true;
                    operand->cc_index = CC_INDEX_C;
                    return;
                default:
                    break;
            }
            break;
        case 'd':
        case 'D':
            switch(*ptr++) {
                case 0:
                    operand->reg = R_D;
                    operand->reg_index = R_INDEX_D;
                    return;
                case 'e':
                case 'E':
                    if((*ptr == 0) || isspace(*ptr)) {
                        operand->reg = R_DE;
                        operand->reg_index = R_INDEX_DE;
                        return;
                    }
                    break;
                default:
                    break;
            }
            break;
        case 'e':
        case 'E':
            if(*ptr++ == 0) {
                operand->reg = R_E;
                operand->reg_index = R_INDEX_E;
                return;
            }
            break;
        case 'h':
        case 'H':
            switch(*ptr++) {
                case 0:
                    operand->reg = R_H;
                    operand->reg_index = R_INDEX_H;
                    return;
                case 'l':
                case 'L':
                    if((*ptr == 0) || isspace(*ptr)) {
                        operand->reg = R_HL;
                        operand->reg_index = R_INDEX_HL;
                        return;
                    }
                    break;
                default:
                    break;
            }
            break;
        case 'i':
        case 'I':
            switch(*ptr++) {
                case 0:
                    operand->reg = R_I;
                    operand->reg_index = R_INDEX_I;
                    return;
                case 'x':
                case 'X':
                    while(isspace(*ptr)) ptr++; // eat spaces
                    switch(*ptr++) {
                        case 0:
                            operand->reg = R_IX;
                            operand->reg_index = R_INDEX_IX;
                            return;
                        case 'h':
                        case 'H':
                            if(*ptr == 0) {
                                operand->reg = R_IXH;
                                return;
                            }
                            break;
                        case 'l':
                        case 'L':
                            if(*ptr == 0) {
                                operand->reg = R_IXL;
                                return;
                            }
                            break;
                        case '+':
                        case '-':
                            operand->reg = R_IX;
                            operand->displacement_provided = true;
                            if(*(ptr-1) == '-') operand->displacement = -1 * (int16_t) getExpressionValue(ptr, REQUIRED_LASTPASS);
                            else operand->displacement = (int16_t) getExpressionValue(ptr, REQUIRED_LASTPASS);
                            return;
                            break;
                        default:
                            break;
                    }
                    break;
                case 'y':
                case 'Y':
                    while(isspace(*ptr)) ptr++; // eat spaces
                    switch(*ptr++) {
                        case 0:
                            operand->reg = R_IY;
                            operand->reg_index = R_INDEX_IY;
                            return;
                        case 'h':
                        case 'H':
                            if(*ptr == 0) {
                                operand->reg = R_IYH;
                                return;
                            }
                            break;
                        case 'l':
                        case 'L':
                            if(*ptr == 0) {
                                operand->reg = R_IYL;
                                return;
                            }
                            break;
                        case '+':
                        case '-':
                            operand->reg = R_IY;
                            operand->displacement_provided = true;
                            if(*(ptr-1) == '-') operand->displacement = -1 * (int16_t) getExpressionValue(ptr, REQUIRED_LASTPASS);
                            else operand->displacement = (int16_t) getExpressionValue(ptr, REQUIRED_LASTPASS);
                            return;
                            break;
                        default:
                            break;
                    }
                    break;
                default:
                    break;
            }
            break;
        case 'l':
        case 'L':
            if(*ptr == 0) {
                operand->reg = R_L;
                operand->reg_index = R_INDEX_L;
                return;
            }
            break;
        case 'm':
        case 'M':
            if((tolower(*ptr) == 'b') && ptr[1] == 0) {
                operand->reg = R_MB;
                operand->reg_index = R_INDEX_MB;
                return;
            }
            if(*ptr == 0) {
                operand->cc = true;
                operand->addressmode |= CC;
                operand->cc_index = CC_INDEX_M;
                return;
            }
            break;
        case 'n':
        case 'N':
            switch(*ptr++) {
                case 'c':   // NC
                case 'C':
                    if(*ptr == 0) {
                        operand->cc = true;
                        operand->addressmode |= CC;
                        operand->cc_index = CC_INDEX_NC;
                        operand->addressmode |= CCA;
                        return;
                    }
                    break;
                case 'z':   // NZ
                case 'Z':
                    if(*ptr == 0) {
                        operand->cc = true;
                        operand->addressmode |= CC;
                        operand->cc_index = CC_INDEX_NZ;
                        operand->addressmode |= CCA;
                        return;
                    }
                    break;
                default:
                    break;
            }
            break;
        case 'p':
        case 'P':
            switch(*ptr++) {
                case 0:
                    operand->cc = true;
                    operand->addressmode |= CC;
                    operand->cc_index = CC_INDEX_P;
                    return;
                case 'e':
                case 'E':
                    if(*ptr == 0) {
                        operand->cc = true;
                        operand->addressmode |= CC;
                        operand->cc_index = CC_INDEX_PE;
                        return;
                    }
                    break;
                case 'o':
                case 'O':
                    if(*ptr == 0) {
                        operand->cc = true;
                        operand->addressmode |= CC;
                        operand->cc_index = CC_INDEX_PO;
                        return;
                    }
                    break;
                default:
                    break;
            }
            break;
        case 'r':
        case 'R':
            if(*ptr == 0) {
                operand->reg = R_R;
                operand->reg_index = R_INDEX_R;
                return;
            }
            break;
        case 's':
        case 'S':
            if((tolower(*ptr) == 'p') && ptr[1] == 0) {
                operand->reg = R_SP;
                operand->reg_index = R_INDEX_SP;
                return;
            }
            break;
        case 'z':
        case 'Z':
            if(*ptr == 0) {
                operand->cc = true;
                operand->addressmode |= CC;
                operand->cc_index = CC_INDEX_Z;
                operand->addressmode |= CCA;
                return;
            }
            break;
        default:
            break;
    }
    
    if(*string) {
        if(operand->indirect) {
            len--;
            string++;
        }
        strcpy(operand->immediate_name, string);
        operand->immediate = getExpressionValue(string, REQUIRED_LASTPASS);
        operand->immediate_provided = true;
        operand->addressmode |= IMM;
    }
}

// FSM to parse each line into separate components, store in gbl currentline variable
void parseLine(char *src) {
    uint8_t oplength = 0;
    bool asmcmd = false;
    uint8_t state;
    uint8_t argcount = 0;
    streamtoken_t streamtoken;
    bool unknown3rdoperand = true;

    // default current line items
    memset(&currentline, 0, sizeof(currentline));
    memset(&operand1, 0, (sizeof(operand_t) - sizeof(operand1.immediate_name) + 1));
    memset(&operand2, 0, (sizeof(operand_t) - sizeof(operand2.immediate_name) + 1));

    state = PS_START;
    while(true) {
        switch(state) {
            case PS_START:
                if(getMnemonicToken(&streamtoken, src)) {
                    if(streamtoken.terminator == ':') {
                        state = PS_LABEL;
                        break;
                    }
                    state = PS_COMMAND;
                    break;
                }
                state = PS_COMMENT;
                break;
            case PS_LABEL:
                currentline.label = streamtoken.start;
                advanceAnonymousLabel();
                if(getMnemonicToken(&streamtoken, streamtoken.next)) {
                    if(streamtoken.terminator == ':') {
                        error(message[ERROR_SYNTAX],0);
                        break;
                    }
                    state = PS_COMMAND;                
                    break;
                }
                state = PS_COMMENT;
                break;
            case PS_COMMAND:
                if(streamtoken.start[0] == '.') {
                    // should be an assembler command
                    asmcmd = true;
                    currentline.mnemonic = streamtoken.start;
                }
                else parse_command(streamtoken.start); // ez80 split suffix and set mnemonic for search

                currentline.current_instruction = instruction_lookup(currentline.mnemonic);
                if(currentline.current_instruction == NULL) {
                    if(!asmcmd) {
                        error(message[ERROR_INVALIDMNEMONIC],"%s",currentline.mnemonic);
                        return;
                    }
                    // Check for assembler command
                    currentline.mnemonic = streamtoken.start + 1;
                    currentline.current_instruction = instruction_lookup(currentline.mnemonic);
                    if((currentline.current_instruction == NULL) ||
                       (currentline.current_instruction->type != ASSEMBLER)) {
                        error(message[ERROR_INVALIDMNEMONIC],"%s",currentline.mnemonic);
                        return;
                    }
                    // Valid assembler command found (with a .)
                }
                if((streamtoken.terminator == ';') || (streamtoken.terminator == 0)) 
                    currentline.next = NULL;
                else currentline.next = streamtoken.next;

                switch(currentline.current_instruction->type) {
                    case EZ80:
                        if(currentline.next) {
                            oplength = getOperandToken(&streamtoken, currentline.next);
                            if(oplength) {
                                state = PS_OP;
                                break;
                            }
                        }
                        return; // ignore any comments
                        break;
                    case ASSEMBLER:
                        return;
                    case MACRO:
                        currentline.current_macro = currentline.current_instruction->macro;
                        currentline.current_instruction = NULL;
                        return;
                }
                break;
            case PS_OP:
                argcount++;   
                if(currentExpandedMacro) {
                    macroExpandArg(macro_expansionbuffer, streamtoken.start, currentExpandedMacro);
                    streamtoken.start = macro_expansionbuffer;
                    oplength = strlen(streamtoken.start);
                }
                if(argcount == 1) {
                    parse_operand(streamtoken.start, oplength, &operand1);
                }
                else {
                    parse_operand(streamtoken.start, oplength, &operand2);
                }
                switch(streamtoken.terminator) {
                    case ';':
                        currentline.next = streamtoken.next;
                        state = PS_COMMENT;
                        break;
                    case 0:
                        currentline.next = NULL;
                        return;
                    case ',':
                        if(argcount == 2) {
                            if(unknown3rdoperand && ((strcasecmp(currentline.mnemonic, "res") == 0) || (strcasecmp(currentline.mnemonic, "set") == 0))) {
                                uint8_t bitnumber = str2num(operand1.immediate_name, strlen(operand1.immediate_name)); // cannot rely on first-pass information which returns 0
                                // Handle 3rd operand in undocumented Z80 instructions RES0-7/SET0-7
                                if((!operand1.immediate_provided) || (bitnumber > 7)){
                                    error(message[ERROR_INVALIDBITNUMBER],"%s",operand1.immediate_name);
                                    return;
                                }
                                // now map to the intended undocumented instruction - SET -> SET0-7 or RES -> RES0-7
                                char tmpmnemonicname[MAX_MNEMONIC_SIZE];
                                snprintf(tmpmnemonicname, MAX_MNEMONIC_SIZE, "%s%d", currentline.mnemonic, bitnumber);
                                currentline.current_instruction = instruction_lookup(tmpmnemonicname);
                                if(!currentline.current_instruction) {
                                    error(message[ERROR_INTERNAL],0);
                                    return;
                                }
                                unknown3rdoperand = false; // any next operand should throw an error
                                operand1 = operand2; // switch operands
                                memset(&operand2, 0, (sizeof(operand_t) - sizeof(operand2.immediate_name) + 1));
                                argcount--;
                            }
                            else {
                                error(message[ERROR_TOOMANYARGUMENTS],0);
                                return;
                            }
                        }
                        oplength = getOperandToken(&streamtoken, streamtoken.next);
                        if(oplength == 0) {
                            error(message[ERROR_MISSINGOPERAND],0);
                            return;
                        }
                        break;
                }
                break;
            case PS_COMMENT:
                currentline.comment = currentline.next;
                return;
        }
    }
}

// Parse an immediate value from currentline.next
// services several assembler directives
bool parse_asm_single_immediate(void) {
    streamtoken_t token;

    if(!currentline.next) {
        error(message[ERROR_MISSINGARGUMENT],0);
        return false;
    }
    if(currentExpandedMacro) {
        macroExpandArg(macro_expansionbuffer, currentline.next, currentExpandedMacro);
        currentline.next = macro_expansionbuffer;
    }
    if(getOperandToken(&token, currentline.next) == 0) {
        error(message[ERROR_MISSINGARGUMENT],0);
        return false;
    }
    operand1.immediate = getExpressionValue(token.start, REQUIRED_FIRSTPASS);
    operand1.immediate_provided = true;
    strcpy(operand1.immediate_name, token.start);
    if((token.terminator != 0) && (token.terminator != ';')) {
        error(message[ERROR_TOOMANYARGUMENTS],0);
        return false;
    }
    return true;
}

// Emits list data for the DB/DW/DW24/DW32 etc directives
void handle_asm_data(uint8_t wordtype) {
    int32_t value;
    streamtoken_t token;
    bool expectarg = true;

    if(inConditionalSection == CONDITIONSTATE_FALSE) return;

    definelabel(address);

    while(currentline.next) {
        if(getDefineValueToken(&token, currentline.next)) {
            if(currentExpandedMacro) {
                macroExpandArg(macro_expansionbuffer, token.start, currentExpandedMacro);
                token.start = macro_expansionbuffer;
            }

            if((token.start[0] == '\"') && (wordtype != ASM_DB)) {
                error(message[ERROR_STRING_NOTALLOWED],0);
                return;
            }

            switch(wordtype) {
                case ASM_DB:
                    switch(token.start[0]) {
                        case '\"':
                            emit_quotedstring(token.start);
                            break;
                        default:
                            value = getExpressionValue(token.start, REQUIRED_LASTPASS); // not needed in pass 1
                            if(pass == ENDPASS) validateRange8bit(value, token.start);
                            emit_8bit(value);
                            break;
                    }
                    break;
                case ASM_DW:
                    value = getExpressionValue(token.start, REQUIRED_LASTPASS);
                    if(pass == ENDPASS) validateRange16bit(value, token.start);
                    emit_16bit(value);
                    break;
                case ASM_DW24:
                    value = getExpressionValue(token.start, REQUIRED_LASTPASS);
                    if(pass == ENDPASS) validateRange24bit(value, token.start);
                    emit_24bit(value);
                    break;
                case ASM_DW32:
                    value = getExpressionValue(token.start, REQUIRED_LASTPASS);
                    emit_32bit(value);
                    break;
                default:
                    error(message[ERROR_INTERNAL],0);
                    break;
            }
            expectarg = false;
        }
        else {
            expectarg = true;
            break;
        }
        if(token.terminator == ',') {
            currentline.next = token.next;
            expectarg = true;
        }
        else {
            if((token.terminator != 0) && (token.terminator != ';')) error(message[ERROR_LISTFORMAT],0);
            currentline.next = NULL;
        }
    }
    if(expectarg) error(message[ERROR_MISSINGARGUMENT],0);
}

void handle_asm_equ(void) {
    streamtoken_t token;
    int32_t value;

    if(inConditionalSection == CONDITIONSTATE_FALSE) return;

    if(!currentline.label) {
        error(message[ERROR_MISSINGLABEL],0);
        return;
    }
    if(!currentline.next) {
        error(message[ERROR_MISSINGARGUMENT],0);
        return;
    }
    if(!getDefineValueToken(&token, currentline.next)) {
        error(message[ERROR_MISSINGARGUMENT],0);
        return;
    }
    if((token.terminator != 0) && (token.terminator != ';')) {
        error(message[ERROR_TOOMANYARGUMENTS],0);
        return;
    }

    value = getExpressionValue(token.start, REQUIRED_FIRSTPASS); // might return the value for $, a potentially relocated address
    bool tmprelocate = relocate;
    relocate = false;
    definelabel(value); // define the value, not the relocated address
    relocate = tmprelocate;
}

void handle_asm_adl(void) {
    streamtoken_t token;

    if(inConditionalSection == CONDITIONSTATE_FALSE) return;

    if(!currentline.next) {
        error(message[ERROR_MISSINGARGUMENT],0);
        return;
    }
    if(getDefineValueToken(&token, currentline.next) == 0) {
        error(message[ERROR_MISSINGARGUMENT],0);
        return;
    }
    if(currentExpandedMacro) {
        macroExpandArg(macro_expansionbuffer, token.start, currentExpandedMacro);
        token.start = macro_expansionbuffer;
    }
    if(strcasecmp(token.start, "adl")) {
        error(message[ERROR_INVALIDOPERAND],0);
        return;
    }
    if(!(cputype & BIT_EZ80)) {
       errorCPUtype(ERROR_INVALID_CPU_MODE);
       return;
    }
    if(token.terminator != '=') {
        error(message[ERROR_MISSINGARGUMENT],0);
        return;
    }
    if(!getDefineValueToken(&token, token.next)) {
            error(message[ERROR_MISSINGARGUMENT],0);
        return;
    }
    operand2.immediate = getExpressionValue(token.start, REQUIRED_FIRSTPASS); // needs to be defined in pass 1
    operand2.immediate_provided = true;
    strcpy(operand2.immediate_name, token.start);

    if((operand2.immediate != 0) && (operand2.immediate != 1)) {
        error(message[ERROR_INVALID_ADLMODE],"%s", operand2.immediate_name);
        return;
    }

    adlmode = operand2.immediate;
}

void handle_asm_org(void) {
    uint24_t newaddress;
    
    if(inConditionalSection == CONDITIONSTATE_FALSE) return;

    if(!parse_asm_single_immediate()) return; // get address from next token
    // address needs to be given in pass 1
    newaddress = operand1.immediate;
    if((adlmode == 0) && (newaddress > 0xffff)) {
        error(message[ERROR_ADDRESSRANGE],"%s", operand1.immediate_name); 
        return;
    }
    if((newaddress < address) && (address != start_address)) {
        error(message[ERROR_ADDRESSLOWER], 0);
        return;
    }
    definelabel(address);

    // Skip filling if this is the first .org statement
    if(address == start_address) {
        address = newaddress;
        return;
    }
    // Fill bytes on any subsequent .org statement
    while(address != newaddress) emit_8bit(fillbyte);
}

void handle_asm_include(void) {
    streamtoken_t token;

    if(inConditionalSection == CONDITIONSTATE_FALSE) return;
    
    if(!currentline.next) {
        error(message[ERROR_MISSINGARGUMENT],0);
        return;
    }

    if(getDefineValueToken(&token, currentline.next) == 0) {
        error(message[ERROR_MISSINGARGUMENT],0);
        return;
    }
    if((token.terminator != 0) && (token.terminator != ';')) {
        error(message[ERROR_TOOMANYARGUMENTS],0);
        return;
    }
    if(token.start[0] != '\"') {
        error(message[ERROR_STRINGFORMAT],0);
        return;
    }
    token.start[strlen(token.start)-1] = 0;
    if(strcmp(token.start+1, currentcontentitem->name) == 0) {
        error(message[ERROR_RECURSIVEINCLUDE],0);
        return;
    }
    definelabel(address);
    if((listing) && (pass == ENDPASS)) listEndLine();
    processContent(token.start+1);
    sourcefilecount++;
}

void handle_asm_incbin(void) {
    streamtoken_t token;
    contentitem_t *ci;
    uint24_t n;

    if(inConditionalSection == CONDITIONSTATE_FALSE) return;

    if(!currentline.next) {
        error(message[ERROR_MISSINGARGUMENT],0);
        return;
    }

    if(getDefineValueToken(&token, currentline.next) == 0) {
        error(message[ERROR_MISSINGARGUMENT],0);
        return;
    }

    if(currentExpandedMacro) {
        macroExpandArg(macro_expansionbuffer, token.start, currentExpandedMacro);
        token.start = macro_expansionbuffer;
    }
    if(token.start[0] != '\"') {
        error(message[ERROR_STRINGFORMAT],0);
        return;
    }
    token.start[strlen(token.start)-1] = 0;

    definelabel(address);

    // Prepare content
    if((ci = findContent(token.start+1)) == NULL) {
        if(pass == STARTPASS) {
            ci = insertContent(token.start+1);
            if(ci == NULL) return;
        }
        else return;
    }

    if(pass == STARTPASS) {
        if(!completefilebuffering) {
            ci->fh = ioOpenfile(ci->name, "rb");
            if(ci->fh == 0) return;
            ci->size = ioGetfilesize(ci->fh);
            fclose(ci->fh);
        }
        address += ci->size;
    }
    if(pass == ENDPASS) {
        if(completefilebuffering) {
            if(listing) { // Output needs to pass to the listing through emit_8bit, performance-hit
                for(n = 0; n < ci->size; n++) emit_8bit(ci->buffer[n]);
            }
            else {
                ioWrite(FILE_OUTPUT, ci->buffer, ci->size);
                address += ci->size;
            }
        }
        else {
            char buffer[INPUT_BUFFERSIZE];

            ci->fh = ioOpenfile(ci->name, "rb");
            if(ci->fh == 0) return;
            while(true) {
                ci->bytesinbuffer = fread(buffer, 1, INPUT_BUFFERSIZE, ci->fh);
                if(ci->bytesinbuffer == 0) break;
                if(listing) { // Output needs to pass to the listing through emit_8bit, performance-hit
                    for(n = 0; n < ci->bytesinbuffer; n++) emit_8bit(buffer[n]);
                }
                else {
                    ioWrite(FILE_OUTPUT, buffer, ci->bytesinbuffer);
                    address += ci->bytesinbuffer;
                }
            }
            fclose(ci->fh);
            ci->fh = NULL;
        }
    }
    binfilecount++;
    if((token.terminator != 0) && (token.terminator != ';')) error(message[ERROR_TOOMANYARGUMENTS],0);
}

void handle_asm_blk(uint8_t width) {
    uint24_t num;
    int32_t val = 0;
    streamtoken_t token;

    if(inConditionalSection == CONDITIONSTATE_FALSE) return;

    definelabel(address);

    if(!currentline.next) {
        error(message[ERROR_MISSINGARGUMENT],0);
        return;
    }

    if(getDefineValueToken(&token, currentline.next) == 0) {
        error(message[ERROR_MISSINGARGUMENT],0); // we need at least one value
        return;
    }

    if(currentExpandedMacro) {
        macroExpandArg(macro_expansionbuffer, token.start, currentExpandedMacro);
        token.start = macro_expansionbuffer;
    }

    num = getExpressionValue(token.start, REQUIRED_FIRSTPASS); // <= needs a number of items during pass 1, otherwise addresses will be off later on

    if(token.terminator == ',') {
        if(getDefineValueToken(&token, token.next) == 0) {
            error(message[ERROR_MISSINGARGUMENT],0);
            return;
        }

        if(currentExpandedMacro) {
            macroExpandArg(macro_expansionbuffer, token.start, currentExpandedMacro);
            token.start = macro_expansionbuffer;
        }
        val = getExpressionValue(token.start, REQUIRED_LASTPASS); // value not required in pass 1
    }
    else { // no value given
        if((token.terminator != 0)  && (token.terminator != ';'))
            error(message[ERROR_LISTFORMAT],0);
        val = fillbyte;
    }
    while(num) {
        switch(width) {
            case 0:
                address += num;
                remaining_dsspaces += num;
                num = 0;
                if(val != fillbyte) warning(message[WARNING_UNSUPPORTED_INITIALIZER],"%s",token.start);
                break;
            case 1:
                if(pass == ENDPASS) validateRange8bit(val, token.start);
                emit_8bit(val);
                num -= 1;
                break;
            case 2:
                if(pass == ENDPASS) validateRange16bit(val, token.start);
                emit_16bit(val);
                num -= 1;
                break;
            case 3:
                if(pass == ENDPASS) validateRange24bit(val, token.start);
                emit_24bit(val);
                num -= 1;
                break;
            case 4:
                emit_32bit(val);
                num -= 1;
                break;
        }
    }
}

void handle_asm_align(void) {
uint24_t alignment;
uint24_t base;
uint24_t delta;

    if(inConditionalSection == CONDITIONSTATE_FALSE) return;

    if(!parse_asm_single_immediate()) return;
    if(operand1.immediate <= 0) {
        error(message[ERROR_ZEROORNEGATIVE],"%s",operand1.immediate_name);
        return;
    }

    if((operand1.immediate & (operand1.immediate - 1)) != 0) {
        error(message[ERROR_POWER2],"%s",operand1.immediate_name); 
        return;
    }
    
    alignment = operand1.immediate;
    base = (~(operand1.immediate - 1) & address);

    if(address & (operand1.immediate -1)) base += alignment;
    delta = base - address;
    remaining_dsspaces += delta;
    address = base;

    definelabel(address); // set address to current line
}

void handle_asm_definemacro(void) {
    uint8_t argcount;
    char *macrobuffer = NULL;
    char arglist[MACROMAXARGS][MACROARGLENGTH + 1];
    char *macroname;
    uint16_t originlinenumber = currentcontentitem->currentlinenumber;

    if(inConditionalSection == CONDITIONSTATE_FALSE) return;

    definelabel(address);

    macrobuffer = readMacroBody(currentcontentitem); // dynamically allocated during STARTPASS

    if(pass == STARTPASS) {
        if(!macrobuffer) return;
        if(!parseMacroDefinition(currentline.next, &macroname, &argcount, (char *)arglist)) return;
        if(!storeMacro(macroname, macrobuffer, argcount, (char *)arglist, originlinenumber)) {
            error(message[ERROR_MACROMEMORYALLOCATION],0);
            return;
        }
    }
}

void handle_asm_cpu(void) {
    streamtoken_t token;

    if(inConditionalSection == CONDITIONSTATE_FALSE) return;

    definelabel(address);

    if(!currentline.next || (getOperandToken(&token, currentline.next) == 0)) {
        error(message[ERROR_MISSINGARGUMENT],0);
        return;
    }
    if(strcasecmp(token.start, "Z80") == 0) {
        cputype = CPU_Z80;
        adlmode = 0;
        return;
    }
    if(strcasecmp(token.start, "Z180") == 0) {
        cputype = CPU_Z180;
        adlmode = 0;
        return;
    }
    if(strcasecmp(token.start, "EZ80") == 0) {
        cputype = CPU_EZ80;
        adlmode = 1;
        return;
    }

    error(message[ERROR_UNSUPPORTED_CPU], "%s", token.start);
}

void handle_asm_if(void) {
    streamtoken_t token;
    int24_t value;
    
    // No nested conditionals.
    if(inConditionalSection != CONDITIONSTATE_NORMAL) {
        error(message[ERROR_NESTEDCONDITIONALS],0);
        return;
    }
    if(!currentline.next) {
        error(message[ERROR_MISSINGARGUMENT],0);
        return;
    }
    if(getDefineValueToken(&token, currentline.next) == 0) {
        error(message[ERROR_CONDITIONALEXPRESSION],0);
        return;
    }

    value = getExpressionValue(token.start, REQUIRED_FIRSTPASS);
    inConditionalSection = value ? CONDITIONSTATE_TRUE : CONDITIONSTATE_FALSE;
}

void handle_asm_else(void) {
    // No nested conditionals.
    if(inConditionalSection == CONDITIONSTATE_NORMAL) {
        error(message[ERROR_MISSINGIFCONDITION],0);
        return;
    }
    inConditionalSection = inConditionalSection == CONDITIONSTATE_FALSE ? CONDITIONSTATE_TRUE : CONDITIONSTATE_FALSE;
}

void handle_asm_endif(void) {
    if(inConditionalSection == CONDITIONSTATE_NORMAL) {
        error(message[ERROR_MISSINGIFCONDITION],0);
        return;
    }
    inConditionalSection = CONDITIONSTATE_NORMAL;
}

void handle_asm_fillbyte(void) {

    if(inConditionalSection == CONDITIONSTATE_FALSE) return;

    if(!parse_asm_single_immediate()) return; // get fillbyte from next token
    if((!ignore_truncation_warnings) && ((operand1.immediate < -128) || (operand1.immediate > 255))) {
        warning(message[WARNING_TRUNCATED_8BIT],"%s",operand1.immediate_name);
    }
    fillbyte = operand1.immediate;
}

void handle_asm_relocate(void) {

    if(relocate) {
        error(message[ERROR_NESTEDRELOCATE],0);
        return;
    }
    if(!parse_asm_single_immediate()) return;

    if((operand1.immediate < 0) || (operand1.immediate > 0xFFFFFF)) {
        error(message[ERROR_ADDRESSRANGE24BIT], 0);
        return;
    }
    relocate = true;
    relocateOutputBaseAddress = address;
    relocateBaseAddress = operand1.immediate;
}

void handle_asm_endrelocate(void) {

    if(!relocate) {
        error(message[ERROR_MISSINGRELOCATE], 0);
        return;
    }
    relocate = false;
    relocateOutputBaseAddress = 0;
    relocateBaseAddress = 0;
}

void handle_assembler_command(void) {
    switch(currentline.current_instruction->asmtype) {
        case(ASM_ADL):
            handle_asm_adl();
            break;
        case(ASM_ORG):
            handle_asm_org();
            break;
        case(ASM_DB):
            handle_asm_data(ASM_DB);
            break;
        case(ASM_DS):
            handle_asm_blk(0);
            break;
        case(ASM_DW):
            handle_asm_data(ASM_DW);
            break;
        case(ASM_DW24):
            handle_asm_data(ASM_DW24);
            break;
        case(ASM_DW32):
            handle_asm_data(ASM_DW32);
            break;
        case(ASM_ASCIZ):
            handle_asm_data(ASM_DB);
            if(inConditionalSection != CONDITIONSTATE_FALSE) emit_8bit(0);
            break;
        case(ASM_EQU):
            handle_asm_equ();
            break;
        case(ASM_INCLUDE):
            handle_asm_include();
            break;
        case(ASM_BLKB):
            handle_asm_blk(1);
            break;
        case(ASM_BLKW):
            handle_asm_blk(2);
            break;
        case(ASM_BLKP):
            handle_asm_blk(3);
            break;
        case(ASM_BLKL):
            handle_asm_blk(4);
            break;
        case(ASM_ALIGN):
            handle_asm_align();
            break;
        case(ASM_MACRO_START):
            handle_asm_definemacro();
            break;
        case(ASM_INCBIN):
            handle_asm_incbin();
            break;
        case(ASM_FILLBYTE):
            handle_asm_fillbyte();
            break;
        case(ASM_CPU):
            handle_asm_cpu();
            break;
        case(ASM_RELOCATE):
            handle_asm_relocate();
            break;
        case(ASM_ENDRELOCATE):
            handle_asm_endrelocate();
            break;
        case(ASM_IF):
            handle_asm_if();
            break;
        case(ASM_ELSE):
            handle_asm_else();
            break;
        case(ASM_ENDIF):
            handle_asm_endif();
            break;
        case(ASM_MACRO_END):
            if(inConditionalSection == CONDITIONSTATE_NORMAL) {
                error(message[ERROR_MACRONOTSTARTED],0);
            }
            break;
    }
    return;
}

// Process the instructions found at each line, after parsing them
void processInstructions(void){
    operandlist_t *list;
    uint8_t listitem;
    bool match;
    bool condmatch;
    bool regamatch, regbmatch;

    if((currentline.mnemonic == NULL) && (inConditionalSection != CONDITIONSTATE_FALSE)) definelabel(address);

    if(currentline.current_instruction) {
        if(currentline.current_instruction->type == EZ80) {
            if(inConditionalSection != CONDITIONSTATE_FALSE) {
                // process this mnemonic by applying the instruction list as a filter to the operand-set
                list = currentline.current_instruction->list;
                match = false;
                for(listitem = 0; listitem < currentline.current_instruction->listnumber; listitem++) {
                    regamatch = (list->regsetA & operand1.reg) || !(list->regsetA | operand1.reg);
                    regbmatch = (list->regsetB & operand2.reg) || !(list->regsetB | operand2.reg);

                    condmatch = ((list->conditionsA & MODECHECK) == operand1.addressmode) && ((list->conditionsB & MODECHECK) == operand2.addressmode);
                    if(list->flags & F_CCOK) {
                        condmatch |= operand1.cc;
                        regamatch = true;
                    }
                    if(regamatch && regbmatch && condmatch) {
                        match = true;
                        if(!(cputype & list->cpu)) {
                            errorCPUtype(ERROR_INVALID_CPU_INSTRUCTION);
                            break;
                        }
                        emit_instruction(list);
                        break;
                    }
                    list++;
                }
                if(!match) error(message[ERROR_OPERANDSNOTMATCHING],0);
                return;
            }
        }
        else handle_assembler_command();
    }
    return;
}

void processMacro(void) {
    macro_t *localexpandedmacro = currentline.current_macro;
    char macroline[LINEMAX+1];
    char substitutionlist[MACROMAXARGS][MACROARGSUBSTITUTIONLENGTH + 1]; // temporary storage for substitutions during expansion <- needs to remain here for recursive macro processing
    char *macrolineptr, *lastmacrolineptr;
    unsigned int localmacrolinenumber;
    bool macro_invocation_warning = false;
    uint24_t localmacroExpandID;
    bool processednestedmacro = false;

    if((listing) && (pass == ENDPASS)) listEndLine();

    // Set counters and local expansion scope
    macrolevel++;
    if(pass == STARTPASS) macroexpansions++;
    localmacroExpandID = macroExpandID++;
    localexpandedmacro->currentExpandID = localmacroExpandID;

    // Check for defined label
    definelabel(address);

    // potentially transform arguments first, when calling from within a macro
    if(currentExpandedMacro) {
        macroExpandArg(macro_expansionbuffer, currentline.next, currentExpandedMacro);
        currentline.next = macro_expansionbuffer;
    }
    currentExpandedMacro = localexpandedmacro;

    if(!(parseMacroArguments(localexpandedmacro, currentline.next, substitutionlist))) return;

    // open macro storage
    macrolineptr = localexpandedmacro->body;

    // process body
    macrolinenumber = 1;
    lastmacrolineptr = macrolineptr;
    while(getnextMacroLine(&macrolineptr, macroline)) {
        if(pass == ENDPASS && (listing)) listStartLine(macroline, macrolinenumber);
        parseLine(macroline);

        if(!currentline.current_macro) processInstructions();
        else {
            // CALL nested macro instruction
            if(macrolevel >= MACRO_MAXLEVEL) {
                error(message[ERROR_MACROMAXLEVEL],"%d",MACRO_MAXLEVEL);
                return;
            }
            localmacrolinenumber = macrolinenumber;
            processMacro();
            processednestedmacro = true;
            // return to 'current' macro level content
            currentExpandedMacro = localexpandedmacro;
            macrolinenumber = localmacrolinenumber;
            localexpandedmacro->currentExpandID = localmacroExpandID;
        }
        if(errorcount || issue_warning) {
            if(processednestedmacro) {
                colorPrintf(errorcount?DARK_RED:DARK_YELLOW, "Invoked from Macro [%s] in \"%s\" line %d as\n", localexpandedmacro->name, localexpandedmacro->originfilename, localexpandedmacro->originlinenumber+localmacrolinenumber);
                processednestedmacro = false;
            }
            getnextMacroLine(&lastmacrolineptr, macroline);
            trimRight(macroline);
            macroExpandArg(macro_expansionbuffer, macroline, localexpandedmacro);
            colorPrintf(DARK_YELLOW, "%s\n",macro_expansionbuffer);
            if(issue_warning) {
                macro_invocation_warning = true; // flag to upstream caller that there was at least a single warning
                issue_warning = false; // disable further LOCAL warnings until they occur
            }
            if(errorcount) return;
        }

        if((listing) && (pass == ENDPASS)) listEndLine();
        macrolinenumber++;
        lastmacrolineptr = macrolineptr;
    }
    // end processing
    currentExpandedMacro = NULL;
    if(macro_invocation_warning) issue_warning = true; // display invocation warning at upstream caller

    macrolevel--;
}

bool increasecontentlevel(void) {
    if(++contentlevel == MAXPROCESSDEPTH) {
        error(message[ERROR_MAXINCLUDEFILES], "%d", MAXPROCESSDEPTH);
        return false;
    }
    if(contentlevel > maxstackdepth) maxstackdepth = contentlevel;
    return true;
}

void decreasecontentlevel(void) {
    contentlevel--;
}

void processContent(const char *filename) {
    char line[LINEMAX+1];      // Temp line buffer, will be deconstructed during streamtoken_t parsing
    char iobuffer[INPUT_BUFFERSIZE];
    contentitem_t *ci;
    bool processedmacro = false;
    contentitem_t *callerci = currentcontentitem;

    if(!increasecontentlevel()) return;
    
    if((ci = findContent(filename)) == NULL) {
        if(pass == STARTPASS) {
            ci = insertContent(filename);
            if(ci == NULL) return;
        }
        else return;
    }
    openContentInput(ci, iobuffer);
    // Process
    while(getnextContentLine(line, ci)) {
        ci->currentlinenumber++;
        if((listing) && (pass == ENDPASS)) listStartLine(line, ci->currentlinenumber);

        parseLine(line);

        if(!currentline.current_macro) processInstructions();
        else {
            processMacro();
            processedmacro = true;
        }
        if(errorcount || issue_warning) {
            getlastContentLine(line, ci);
            if(processedmacro) {
                colorPrintf(errorcount?DARK_RED:DARK_YELLOW, "Invoked from \"%s\" line %d as\n", filename, ci->currentlinenumber);
                processedmacro = false;
            }
            if(issue_warning || (contentlevel == errorreportlevel)) colorPrintf(DARK_YELLOW, "%s", line);            
            issue_warning = false;
            if(errorcount) {
                closeContentInput(ci, callerci);
                decreasecontentlevel();
                return;
            }
        }
        if((listing) && (pass == ENDPASS)) listEndLine();
    }
    if(inConditionalSection != CONDITIONSTATE_NORMAL) {
        error(message[ERROR_MISSINGENDIF],0);
        return;
    }
    closeContentInput(ci, callerci);
    decreasecontentlevel();
    strcpy(ci->labelscope, ""); // empty scope for next pass
}

// Initialize pass 1 / pass2 states for the assembler
void passInitialize(uint8_t passnumber) {
    pass = passnumber;
    address = start_address;
    currentExpandedMacro = NULL;
    inConditionalSection = CONDITIONSTATE_NORMAL;
    contentlevel = 0;
    sourcefilecount = 1;
    binfilecount = 0;
    issue_warning = false;
    remaining_dsspaces = 0;
    macrolevel = 0;
    macroExpandID = 0;
    relocate = false;
    relocateOutputBaseAddress = 0;
    relocateBaseAddress = 0;
    currentcontentitem = NULL;

    initAnonymousLabelTable();
        if(pass == ENDPASS) {
        fseek(filehandle[FILE_ANONYMOUS_LABELS], 0, 0);
        readAnonymousLabel();
        listInit();
    }
}

void assemble(const char *filename) {

    for(uint8_t p = STARTPASS; p <= ENDPASS; p++) {
        printf("Pass %d...\n", p);
        passInitialize(p);
        processContent(filename);
        if(errorcount) return;
    }
}
