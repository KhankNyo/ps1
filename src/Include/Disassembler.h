#ifndef R3000A_DISASSEMBLER_H
#define R3000A_DISASSEMBLER_H

#include "Common.h"

#define DISASM_BEAUTIFUL_REGNAME (u32)(1ul << 0)
#define DISASM_IMM16_AS_HEX (u32)(1lu << 1)
#define DISASM_HEX_OFFSET (u32)(1ul << 2)
/* returns the string length written (excluding null terminator) */
/* generally, a buffer of 64 bytes or more will not result in truncation */
int Disassemble(
    u32 Instruction, 
    u32 CurrentPC, 
    u32 Flags, 
    char *OutBuffer, 
    iSize OutBufferSize
);


#endif /* R3000A_DISASSEMBLER_H */

