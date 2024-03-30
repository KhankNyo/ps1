#ifndef R051_DISASSEMBLER_C
#define R051_DISASSEMBLER_C

#include "Common.h"


#define DISASM_BEAUTIFUL_REGNAME (u32)(1 << 0)
#define DISASM_IMM16_AS_HEX (u32)(1 << 1)
/* generally, a buffer of 64 bytes or more will not result in truncation */
void R3051_Disasm(
    u32 Instruction, 
    u32 CurrentPC, 
    u32 Flags, 
    char *OutBuffer, 
    iSize OutBufferSize
);


/* =============================================================================================
 *
 *                                       IMPLEMENTATION 
 *
 *=============================================================================================*/

#include <stdio.h> /* snprintf */



static const char *sR3051_BeautifulRegisterName[32] = {
    "zero", 
    "at", 
    "v0", "v1", 
    "a0", "a1", "a2", "a3",
    "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
    "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
    "t8", "t9",
    "k0", "k1",
    "gp", "sp", "fp", /* uhhh TODO: this could be s8 */
    "ra",
};
static const char *sR3051_RegisterName[32] = {
    "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
    "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
    "r16", "r17", "r18", "r19", "r20", "r21", "r22", "r23",
    "r24", "r25", "r26", "r27", "r28", "r29", "r30", "r31",
};


static void R3051_DisasmSpecial(u32 Instruction, const char **RegName, char *OutBuffer, iSize OutBufferSize)
{
    uint Group = FUNCT_GROUP(Instruction);
    uint Mode = FUNCT_MODE(Instruction);
    const char *Rs = RegName[REG(Instruction, RS)];
    const char *Rt = RegName[REG(Instruction, RT)];
    const char *Rd = RegName[REG(Instruction, RD)];

    switch (Group)
    {
    case 0: /* shift group */
    {
        static const char MnemonicTable[][5] = {
            "sll", "???", "srl", "sra", 
            "sllv", "???", "srlv", "srav"
        };
        if (Mode == 1)
            goto UnknownOpcode;

        if (Instruction == 0)
        {
            snprintf(OutBuffer, OutBufferSize, "nop");
        }
        else if (Mode < 4)
        {
            snprintf(OutBuffer, OutBufferSize, "%s %s, %s, %d", 
                MnemonicTable[Mode], Rd, Rt, (Instruction >> 6) & 0x1F
            );
        }
        else
        {
            snprintf(OutBuffer, OutBufferSize, "%s %s, %s, %s", 
                MnemonicTable[Mode], Rd, Rt, Rs /* NOTE: order in assembly: rd, rt, rs */
            );
        }
    } break;
    case 1: /* jr, jalr, syscall, break */
    {
        switch (Mode)
        {
        case 0: snprintf(OutBuffer, OutBufferSize, "jr %s", Rs); break;
        case 1: snprintf(OutBuffer, OutBufferSize, "jalr %s, %s", Rs, Rd); break;
        case 4: snprintf(OutBuffer, OutBufferSize, "syscall %5x", (Instruction >> 6) & 0xFFFFF); break;
        case 5: snprintf(OutBuffer, OutBufferSize, "break %5x", (Instruction >> 6) & 0xFFFFF); break;
        default: goto UnknownOpcode;
        }
    } break;
    case 2: /* mfhi, mflo, mthi, mtlo */
    {
        static const char MnemonicTable[][5] = {
            "mfhi", "mthi", "mflo", "mtlo"
        };
        const char *Reg = Mode & 0x1?  /* 'from' instructions? */
            Rd: Rs;

        if (Mode > 3) 
            goto UnknownOpcode;
        snprintf(OutBuffer, OutBufferSize, "%s %s", 
            MnemonicTable[Mode], Reg
        );
    } break;
    case 3: /* mult, multu, div, divu */
    {
        const char MnemonicTable[][6] = {
            "mult", "multu", "div", "divu"
        };

        if (Mode > 3)
            goto UnknownOpcode;
        snprintf(OutBuffer, OutBufferSize, "%s %s, %s", 
            MnemonicTable[Mode], Rs, Rt
        );
    } break;
    case 4: /* arith */
    {
        const char MnemonicTable[][5] = {
            "add", "addu", "sub", "subu", 
            "and", "or", "xor", "nor"
        };
        snprintf(OutBuffer, OutBufferSize, "%s %s, %s, %s", 
            MnemonicTable[Mode], Rd, Rs, Rt
        );
    } break;
    case 5: /* compare */
    {
        if (Mode == 2)
        {
            snprintf(OutBuffer, OutBufferSize, "slt %s, %s, %s", 
                Rd, Rs, Rt
            );
        }
        else if (Mode == 3)
        {
            snprintf(OutBuffer, OutBufferSize, "sltu %s, %s, %s", 
                Rd, Rs, Rt
            );
        }
        else goto UnknownOpcode;
    } break;

UnknownOpcode:
    default:
    {
        snprintf(OutBuffer, OutBufferSize, "???");
    } break;
    }
}



void R3051_Disasm(u32 Instruction, u32 CurrentPC, u32 Flags, char *OutBuffer, iSize OutBufferSize)
{
    /*
     * https://stuff.mit.edu/afs/sipb/contrib/doc/specs/ic/cpu/mips/r3051.pdf
     *      INSTRUCTION SET ARCHITECTURE CHAPTER 2
     *      table 2.10: Opcode Encoding
     * */

    const char **RegName = Flags & DISASM_BEAUTIFUL_REGNAME? 
        sR3051_BeautifulRegisterName 
        : sR3051_RegisterName;
    uint OpcodeGroup = OP_GROUP(Instruction);
    uint OpcodeMode = OP_MODE(Instruction);
    CurrentPC &= ~0x0003;
    switch (OpcodeGroup)
    {
    case 0: /* group 0 */
    {
        switch (OpcodeMode)
        {
        case 0: /* special */
        {
            R3051_DisasmSpecial(Instruction, RegName, OutBuffer, OutBufferSize);
        } break;
        case 1: /* bcond */
        {
            uint RtBits = REG(Instruction, RT);
            i32 BranchOffset = (i32)(i16)(Instruction & 0xFFFF)*4;
            u32 DelaySlotAddr = CurrentPC + 4;
            const char *Rs = RegName[REG(Instruction, RS)];
            const char *Mnemonic = "???";

            switch (RtBits)
            {
            case 000: Mnemonic = "bltz"; break;
            case 001: Mnemonic = "bgez"; break;
            case 020: Mnemonic = "bltzal"; break;
            case 021: Mnemonic = "bgezal"; break;
            default: goto UnknownOpcode;
            }

            snprintf(OutBuffer, OutBufferSize, "%s %s, 0x%08x", 
                Mnemonic, Rs, (u32)(DelaySlotAddr + BranchOffset)
            );
        } break;
        case 2: /* j (jump) */
        {
            u32 TargetMask = 0x03FFFFFF;
            u32 DelaySlotAddr = CurrentPC + 4;
            u32 TargetAddr = (Instruction & TargetMask) << 2;
            u32 Target = (DelaySlotAddr & 0xF0000000) | TargetAddr;

            snprintf(OutBuffer, OutBufferSize, "j 0x%08x", Target);
        } break;
        case 3: /* jal */
        {
            u32 TargetMask = 0x03FFFFFF;
            u32 DelaySlotAddr = CurrentPC + 4;
            u32 TargetAddr = (Instruction & TargetMask) << 2;
            u32 Target = (DelaySlotAddr & 0xF0000000) | TargetAddr;

            snprintf(OutBuffer, OutBufferSize, "jal 0x%08x", Target);
        } break;
        default: /* branch, mode from 4 to 7 */
        {
            static const char MnemonicTable[][5] = {
                [4] = "beq", "bne", "blez", "bgtz"
            };
            const char *Rs = RegName[REG(Instruction, RS)];
            const char *Rt = RegName[REG(Instruction, RT)];
            i32 DelaySlotAddr = CurrentPC + 4;
            i32 BranchOffset = (i32)(i16)(Instruction & 0xFFFF)*4;

            if (OpcodeMode < 6) /* reg-reg comparison */
            {
                snprintf(OutBuffer, OutBufferSize, "%s %s, %s, 0x%08x", 
                    MnemonicTable[OpcodeMode], Rs, Rt, (u32)(DelaySlotAddr + BranchOffset)
                );
            }
            else /* reg-zero, NOTE: the comparison type is different, can't use reg-reg comparison */
            {
                snprintf(OutBuffer, OutBufferSize, "%s %s, 0x%08x", 
                    MnemonicTable[OpcodeMode], Rs, (u32)(DelaySlotAddr + BranchOffset)
                );
            }
        } break;
        }
    } break;
    case 1: /* immediate group */
    {
        static const char MnemonicTable[][6] = {
            "addi", "addiu", "slti", "sltiu", 
            "andi", "ori", "xori", "lui"
        };
        const char *Mnemonic = MnemonicTable[OpcodeMode];
        const char *Rt = RegName[REG(Instruction, RT)];
        i32 Immediate = (i32)(i16)(Instruction & 0xFFFF);

        if (OpcodeMode == 7) /* lui doesn't have rs */
        {
            snprintf(OutBuffer, OutBufferSize, "lui %s, 0x%04x", Rt, Immediate & 0xFFFF);
        }
        else
        {
            const char *Rs = RegName[REG(Instruction, RS)];

            if (Flags & DISASM_IMM16_AS_HEX)
            {
                const char *FormatString = "%s %s, %s 0x%08x";
                if (OpcodeMode >= 4)
                {
                    FormatString = "%s %s, %s 0x%04x";
                    Immediate &= 0xFFFF;
                }
                snprintf(OutBuffer, OutBufferSize, FormatString, 
                    Mnemonic, Rt, Rs, (u32)Immediate
                );
            }
            else
            {
                if (OpcodeMode >= 4)
                {
                    Immediate &= 0xFFFF;
                }
                snprintf(OutBuffer, OutBufferSize, "%s %s, %s, %d", 
                    Mnemonic, Rt, Rs, (i32)Immediate
                );
            }
        }
    } break;
    case 2: /* coprocessor */
    {
        if (OpcodeMode == 0) /* CP0 */
        {
            static const char *CP0Register[32] = {
                "r0", "r1", "r2", "BPC", 
                "r4", "BDA", "JUMPDEST", "DCIC", 
                "BadVAddr", "BDAM", "r10", "BPCM", 
                "SR", "CAUSE", "EPC", "PRID", 
                "r16", "r17", "r18", "r19",
                "r20", "r21", "r22", "r23",
                "r24", "r25", "r26", "r27",
                "r28", "r29", "r30", "r31"
            };
            switch (REG(Instruction, RS))
            {
            case 0x00:
            case 0x01: /* mcf0 */
            {
                const char *Rt = RegName[REG(Instruction, RT)];
                const char *Rd = CP0Register[REG(Instruction, RD)];
                snprintf(OutBuffer, OutBufferSize, "mfc0 %s, cp0_%s", Rt, Rd);
            } break;
            case 0x04:
            case 0x05: /* mtc0 */
            {
                const char *Rt = RegName[REG(Instruction, RT)];
                const char *Rd = CP0Register[REG(Instruction, RD)];
                snprintf(OutBuffer, OutBufferSize, "mtc0 %s, cp0_%s", Rt, Rd);
            } break;
            default: goto UnknownOpcode;
            }
        }
        else if (OpcodeMode == 2) /* CP2 */
        {
            TODO("Disassemble CP2");
        }
        else goto UnknownOpcode;
    } break;
    case 4: /* load */
    {
        static const char MnemonicTable[][4] = {
            "lb", "lh", "lwl", "lw",
            "lbu", "lhu", "lwr", "???"
        };
        const char *Base = RegName[REG(Instruction, RS)];
        const char *Rt = RegName[REG(Instruction, RT)];
        i32 Offset = (i32)(i16)(Instruction & 0xFFFF);
        if (OpcodeMode == 7)
            goto UnknownOpcode;

        snprintf(OutBuffer, OutBufferSize, "%s %s, %d(%s)", 
            MnemonicTable[OpcodeMode], Rt, Offset, Base
        );
    } break;
    case 5: /* store */
    {
        static const char MnemonicTable[][4] = {
            "sb", "sh", "swl", "sw",
            "???", "???", "swr", "???"
        };
        const char *Base = RegName[REG(Instruction, RS)];
        const char *Rt = RegName[REG(Instruction, RT)];
        u32 Offset = (i32)(i16)(Instruction & 0xFFFF);
        if (OpcodeMode == 7 || OpcodeMode == 4 || OpcodeMode == 5)
            goto UnknownOpcode;

        snprintf(OutBuffer, OutBufferSize, "%s %s, %d(%s)", 
            MnemonicTable[OpcodeMode], Rt, Offset, Base
        );
    } break;

UnknownOpcode:
    case 3:
    /* since the ps1 does not have these instruction, disassemble them as unknowns */
    case 6: /* load (coprocessor) */
    case 7: /* store (coprocessor) */
    default:
    {
        snprintf(OutBuffer, OutBufferSize, "???");
    } break;
    }
}







#ifdef STANDALONE

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        printf("Usage: %s <binary file>", argv[0]);
        return 1;
    }

    FILE *f = fopen(argv[1], "rb");
    if (NULL == f)
    {
        perror(argv[1]);
        return 1;
    }

    fseek(f, 0, SEEK_END);
    u32 FileSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    u32 *Program = malloc(FileSize);
    if (NULL == Program)
    {
        perror("malloc");
        goto CloseFile;
    }

    u32 InstructionCount = FileSize / sizeof(u32);
    if (fread(Program, 1, FileSize, f) != FileSize)
    {
        perror("fread");
        goto Cleanup;
    }

    u32 Flags = DISASM_IMM16_AS_HEX;
    for (iSize i = 0; i < InstructionCount; i++)
    {
        char Line[64];
        R3051_Disasm(
            Program[i], 
            i*4, 
            Flags, 
            Line, 
            sizeof Line
        );
        printf("%8x:  %08x  %s\n", (u32)i*4, Program[i], Line);
    }

Cleanup:
    free(Program);
CloseFile:
    fclose(f);
    return 0;
}

#endif /* STANDALONE */


#endif /* R051_DISASSEMBLER_C */

