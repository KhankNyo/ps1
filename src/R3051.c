#ifndef R3051_C
#define R3051_C

#include "Common.h"

typedef enum R3051_DataSize 
{
    DATA_BYTE = sizeof(u8),
    DATA_HALF = sizeof(u16),
    DATA_WORD = sizeof(u32),
} R3051_DataSize;
typedef void (*R3051Write)(void *UserData, u32 Addr, u32 Data, R3051_DataSize Size);
typedef u32 (*R3051Read)(void *UserData, u32 Addr, R3051_DataSize Size);


typedef struct R3051_Coprocessor0
{
    u32 PRId;
    u32 SR;
    u32 Cause;
    u32 EPC;
    u32 BadVAddr;
    /* rest in the docs is R3041 or R3081 */
} R3051_Coprocessor0;

#define R3051_REG_COUNT 32
#define R3051_PIPESTAGE_COUNT 5
typedef struct R3051 
{
    u32 R[R3051_REG_COUNT];
    u32 Hi, Lo;

    u32 PC;
    u32 PCSave[R3051_PIPESTAGE_COUNT];
    u32 Instruction[R3051_PIPESTAGE_COUNT];
    struct {
        u32 *RegRef;
        u32 Data;
    } WritebackBuffer[R3051_PIPESTAGE_COUNT];
    int PipeStage;
    int HiLoCyclesLeft;
    Bool8 HiLoBlocking;
    Bool8 ExceptionRaised;
    int ExceptionCyclesLeft;

    void *UserData;
    R3051Read ReadFn;
    R3051Write WriteFn;

    R3051_Coprocessor0 CP0;
} R3051;

R3051 R3051_Init(
    void *UserData, 
    R3051Read ReadFn, R3051Write WriteFn
);
void R3051_StepClock(R3051 *This);



/* =============================================================================================
 *
 *                                       IMPLEMENTATION 
 *
 *=============================================================================================*/

/*
 * https://stuff.mit.edu/afs/sipb/contrib/doc/specs/ic/cpu/mips/r3051.pdf
 * INSTRUCTION SET ARCHITECTURE CHAPTER 2
 *      table 2.10: Opcode Encoding
 *
 * https://student.cs.uwaterloo.ca/~cs350/common/r3000-manual.pdf
 * MACHINE INSTRUCTION REFERENCE APPENDIX A
 * */


#define FETCH_STAGE 0
#define DECODE_STAGE 1
#define EXECUTE_STAGE 2
#define MEMORY_STAGE 3
#define WRITEBACK_STAGE 4
#define OVERFLOW_I32(r, a, b) \
    ((u32)(~(a ^ b) & (r ^ a)) >> 31)

#define READ_BYTE(u32Addr) This->ReadFn(This->UserData, u32Addr, DATA_BYTE)
#define READ_HALF(u32Addr) This->ReadFn(This->UserData, u32Addr, DATA_HALF)
#define READ_WORD(u32Addr) This->ReadFn(This->UserData, u32Addr, DATA_WORD)
#define WRITE_BYTE(u32Addr, u8Byte) This->WriteFn(This->UserData, u32Addr, u8Byte, DATA_BYTE)
#define WRITE_HALF(u32Addr, u16Half) This->WriteFn(This->UserData, u32Addr, u16Half, DATA_HALF)
#define WRITE_WORD(u32Addr, u32Word) This->WriteFn(This->UserData, u32Addr, u32Word, DATA_WORD)



R3051 R3051_Init(
    void *UserData, 
    R3051Read ReadFn, R3051Write WriteFn
)
{
    u32 ResetVector = 0xBFC00000;
    R3051 Mips = {
        .PC = ResetVector,
        .PCSave = { 0 },
        .Instruction = { 0 },
        .R = { 0 },

        .UserData = UserData,
        .WriteFn = WriteFn,
        .ReadFn = ReadFn,
    };
    return Mips;
}


static int R3051_CurrentPipeStage(const R3051 *This, int Stage)
{
    int CurrentStage = This->PipeStage - Stage;
    if (CurrentStage < 0)
        CurrentStage += R3051_PIPESTAGE_COUNT;
    if (CurrentStage >= R3051_PIPESTAGE_COUNT)
        CurrentStage -= R3051_PIPESTAGE_COUNT;
    return CurrentStage;
}

static u32 R3051_InstructionAt(const R3051 *This, int Stage)
{
    int CurrentStage = R3051_CurrentPipeStage(This, Stage);
    return This->Instruction[CurrentStage];
}

static void R3051_ScheduleWriteback(R3051 *This, u32 *Location, u32 Data, int Stage)
{
    This->R[0] = 0;
    int CurrentPipeStage = R3051_CurrentPipeStage(This, Stage);
    if (Location && &This->R[0] != Location)
    {
        This->WritebackBuffer[CurrentPipeStage].RegRef = Location;
        This->WritebackBuffer[CurrentPipeStage].Data = Data;
    }
}



/* =============================================================================================
 *                                         FETCH STAGE
 *=============================================================================================*/


static void R3051_Fetch(R3051 *This)
{
    This->PCSave[This->PipeStage] = This->PC;
    This->Instruction[This->PipeStage] = READ_WORD(This->PC);
    This->PC += sizeof(u32);
}



static Bool8 R3051_DecodeSpecial(R3051 *This, u32 Instruction)
{
    Bool8 IsValidInstruction = true;
    switch (FUNCT_GROUP(Instruction))
    {
    case 0: /* invalid shift instructions */
    {
        IsValidInstruction = (FUNCT_MODE(Instruction) != 1 && FUNCT_MODE(Instruction) != 5);
    } break;
    case 1:
    {
        switch (FUNCT_MODE(Instruction))
        {
        case 0: /* jr */
        {
            /* NOTE: unaligned exception is only raised during fetch stage, 
             * unlike jalr where it is raised before execution of the instruction in the branch delay slot */
            This->PC = This->R[REG(Instruction, RS)];
        } break;
        case 1: /* jalr */
        {
            u32 Rs = This->R[REG(Instruction, RS)];
            u32 *Rd = &This->R[REG(Instruction, RD)];

            if (Rs & 0x3) /* unaligned addr, TODO: also check for valid jump addr */
            {
                /* TODO: bad addr exception, 
                 * NOTE: instruction in branch delay slot will not be executed */
            }
            else
            {
                *Rd = This->PC;
                This->PC = Rs;
                R3051_ScheduleWriteback(This, Rd, *Rd, DECODE_STAGE);
            }
        } break;
        case 5: /* syscall */
        {
            /* TODO: syscall */
        } break;
        case 6: /* break */
        {
            /* TODO: break */
        } break;
        default: IsValidInstruction = false; break;
        }

        /* return early here to avoid modifying PC */
        return IsValidInstruction;
    } break;
    case 2: /* hi/lo */
    case 3: /* alu */
    {
        IsValidInstruction = FUNCT_MODE(Instruction) < 4;
    } break;
    case 4: /* alu op */ break;
    case 5: /* comparison */
    {
        IsValidInstruction = (FUNCT_MODE(Instruction) == 2 || FUNCT_MODE(Instruction) == 3);
    } break;
    case 6:
    case 7:
    {
        IsValidInstruction = false;
    } break;
    }
    return IsValidInstruction;
}


/* =============================================================================================
 *                                         DECODE STAGE
 *=============================================================================================*/

/* returns true if an exception was raised during this stage, false otherwise */
static Bool8 R3051_Decode(R3051 *This)
{
#define BRANCH_IF(Cond) \
    if (Cond) \
        This->PC = This->PC - 4 + ((i32)(i16)(Instruction & 0xFFFF) << 2)

    /* NOTE: decode stage determines next PC value and checks for illegal instruction  */
    u32 NextInstructionAddr = This->PC;
    u32 Instruction = R3051_InstructionAt(This, DECODE_STAGE);
    u32 Rs = This->R[REG(Instruction, RS)];
    u32 Rt = This->R[REG(Instruction, RT)];
    Bool8 InstructionIsIllegal = false;

    switch (OP(Instruction))
    {
    case 000: /* special */
    {
        InstructionIsIllegal = !R3051_DecodeSpecial(This, Instruction);
        if (This->ExceptionRaised)
            return true;
    } break;

    case 001: /* bcond */
    {
        /*
         * Bits 17..19 are ignored, no exception is raised 
         * http://problemkaputt.de/psx-spx.htm#cpujumpopcodes
         */
        Bool8 ShouldBranch = false;
        switch (REG(Instruction, RT) & 0x11) /* NOTE: looks at RT reg field */
        {
        case 0x00: /* bltz */ ShouldBranch = (i32)Rs < 0; break;
        case 0x01: /* bgez */ ShouldBranch = (i32)Rs >= 0; break;
        case 0x10: /* bltzal */
        {
            ShouldBranch = (i32)Rs < 0;
            This->R[31] = NextInstructionAddr;
            R3051_ScheduleWriteback(This, &This->R[31], NextInstructionAddr, DECODE_STAGE);
        } break;
        case 0x11: /* bgezal */
        {
            ShouldBranch = (i32)Rs >= 0; 
            This->R[31] = NextInstructionAddr;
            R3051_ScheduleWriteback(This, &This->R[31], NextInstructionAddr, DECODE_STAGE);
        } break;
        }

        BRANCH_IF(ShouldBranch);
    } break;

    case 003: /* jal (jump and link) */
    {
        This->R[31] = NextInstructionAddr;
        R3051_ScheduleWriteback(This, &This->R[31], NextInstructionAddr, DECODE_STAGE);
        goto j; /* goto is used instead of fallthrough to suppress warning */
    }
    case 002: /* j (jump) */
j:
    {
        /* NOTE: these j(al) instructions don't check for validity of target addr unlike jr and jalr */
        u32 AddrMask = 0x03FFFFFF;
        u32 Immediate = (Instruction & AddrMask) << 2;
        This->PC = (This->PC & 0xF0000000) | Immediate;
    } break;

    case 004: /* beq */  BRANCH_IF(Rs == Rt); break;
    case 005: /* bne */  BRANCH_IF(Rs != Rt); break;
    case 006: /* blez */ BRANCH_IF((i32)Rs <= 0); break;
    case 007: /* bgtz */ BRANCH_IF((i32)Rs > 0); break;

    default: 
    {
        u32 OpMode = OP_MODE(Instruction);
        switch (OP_GROUP(Instruction))
        {
        case 06: /* coprocessor load group */
        case 07: /* coprocessor store group */
        case 02: InstructionIsIllegal = OpMode > 3; break; /* coprocessor group */

        case 03: InstructionIsIllegal = true; break; /* illegal */
        case 04: InstructionIsIllegal = OpMode == 07; break; /* load group */
        case 05: InstructionIsIllegal = OpMode > 3 && OpMode != 6; break; /* store group */
        }
    } break;
    }

    if (InstructionIsIllegal)
    {
        /* TODO: raise exception */
        return true;
    }
    return false;
#undef BRANCH_IF 
}


static u32 *R3051_ExecuteSpecial(R3051 *This, u32 Instruction, u32 Rt, u32 Rs)
{
    u32 *Rd = &This->R[REG(Instruction, RD)];
    switch (FUNCT(Instruction)) 
    {
    /* group 0: alu shift */
    case 000: /* sll */
    {
        *Rd = Rt << SHAMT(Instruction);
    } break;
    case 002: /* srl */
    {
        *Rd = Rt >> SHAMT(Instruction);
    } break;
    case 003: /* sra */
    {
        *Rd = Rt & 0x80000000? 
            ~(~(i32)Rt >> SHAMT(Instruction))
            : Rt >> SHAMT(Instruction);
    } break;
    case 004: /* sllv */
    {
        Rs &= 0x1F;
        *Rd = Rt << Rs;
    } break;
    case 006: /* srlv */
    {
        Rs &= 0x1F;
        *Rd = Rt << Rs;
    } break;
    case 007: /* srav */
    {
        Rs &= 0x1F;
        *Rd = Rt & 0x80000000?
            ~(~(i32)Rt >> Rs) 
            : Rt >> Rs;
    } break;


    /* group 2: mthi, mtlo
     * NOTE: reading from hi/lo (happens in the writeback stage) 
     * requires writes to hi/lo to be 2 instructions apart or more 
     * (assuming that the execute stage happens 2 pipes earlier than the writeback stage) 
     */
    case 021: /* mthi */
    {
        This->Hi = Rs;
        return NULL;
    } break;
    case 023: /* mtlo */
    {
        This->Lo = Rs;
        return NULL;
    } break;


    /* group 3: mult(u)/div(u) */
    case 030: /* mult */
    {
        This->HiLoCyclesLeft = 6;
        if (IN_RANGE(0x00000800, Rs, 0x000FFFFF))
            This->HiLoCyclesLeft = 9;
        else if (IN_RANGE(0x00100000, Rs, 0xFFFFFFFF))
            This->HiLoCyclesLeft = 13;

        i64 Result = (i64)(i32)Rt * (i64)(i32)Rs;
        This->Hi = (u64)Result >> 32;
        This->Lo = (u64)Result & 0xFFFFFFFF;
        return NULL;
    } break;
    case 031: /* multu */
    {
        This->HiLoCyclesLeft = 6;
        if (IN_RANGE(0x00000800, Rs, 0x000FFFFF))
            This->HiLoCyclesLeft = 9;
        else if (IN_RANGE(0x00100000, Rs, 0xFFFFFFFF))
            This->HiLoCyclesLeft = 13;

        u64 Result = (u64)Rt * Rs;
        This->Hi = Result >> 32;
        This->Lo = Result & 0xFFFFFFFF;
        return NULL;
    } break;
    case 032: /* div */
    {
        This->HiLoCyclesLeft = 36;
        i32 SignedRt = (i32)Rt;
        i32 SignedRs = (i32)Rs;
        if (SignedRt == 0)
        {
            /* NOTE: returns 1 if sign bit is set, otherwise return -1 */
            This->Lo = Rs & 0x80000000?
                1: -1;
            This->Hi = Rs;
        }
        else if (SignedRt == -1 && Rs == 0x80000000)
        {
            This->Lo = 0x80000000;
            This->Hi = 0;
        }
        else
        {
            This->Lo = SignedRt / SignedRs;
            This->Hi = SignedRt % SignedRs;
        }
        return NULL;
    } break;
    case 033: /* divu */
    {
        This->HiLoCyclesLeft = 36;
        if (Rt == 0)
        {
            This->Lo = 0xFFFFFFFF;
            This->Hi = Rs;
        }
        else
        {
            This->Lo = Rs / Rt;
            This->Hi = Rs % Rt;
        }
        return NULL;
    } break;


    /* group 4: alu */
    case 040: /* add */
    {
        u32 Result = Rs + Rt;
        if (OVERFLOW_I32(Result, Rs, Rt))
        {
            /* TODO: overflow exception */
            return NULL;
        }
        else
        {
            *Rd = Result;
        }
    } break;
    case 041: /* addu */
    {
        *Rd = Rs + Rt;
    } break;
    case 042: /* sub */
    {
        u32 NegatedRt = -Rt;
        u32 Result = Rs + NegatedRt;
        if (OVERFLOW_I32(Result, Rs, NegatedRt))
        {
            /* TODO: overflow exception */
            return NULL;
        }
        else
        {
            *Rd = Result;
        }
    } break;
    case 043: /* subu */
    {
        *Rd = Rs - Rt;
    } break;
    case 044: /* and */
    {
        *Rd = Rs & Rt;
    } break;
    case 045: /* or */
    {
        *Rd = Rs | Rt;
    } break;
    case 046: /* xor */
    {
        *Rd = Rs ^ Rt;
    } break;
    case 047: /* nor */
    {
        *Rd = ~(Rs | Rt);
    } break;

    /* group 5: alu compare */
    case 052: /* slt */
    {
        *Rd = (i32)Rs < (i32)Rt;
    } break;
    case 053: /* sltu */
    {
        *Rd = Rs < Rt;
    } break;

    default: return NULL;
    }

    return Rd;
}




/* =============================================================================================
 *                                         EXECUTE STAGE
 *=============================================================================================*/

/* returns true if an exception has occured during this stage, false otherwise */
static Bool8 R3051_Execute(R3051 *This)
{
    u32 Instruction = R3051_InstructionAt(This, EXECUTE_STAGE);
    u32 *Rt = &This->R[REG(Instruction, RT)];
    u32 Rs = This->R[REG(Instruction, RS)];

    u32 SignedImm = (i32)(i16)(Instruction & 0xFFFF);
    u32 UnsignedImm = Instruction & 0xFFFF;

    /* only care about ALU ops */
    switch (OP(Instruction)) /* group and mode of op */
    {
    case 000:
    {
        Rt = R3051_ExecuteSpecial(This, Instruction, *Rt, Rs);
        if (This->ExceptionRaised)
            return true;
    } break;

    /* group 1: alu immediate */
    case 010: /* addi */
    {
        u32 Result = Rs + SignedImm;
        if (OVERFLOW_I32(Result, Rs, SignedImm))
        {
            /* TODO: overflow exception */
            Rt = NULL;
            return true;
        }
        else
        {
            *Rt = Result;
        }
    } break;
    case 011: /* addiu */
    {
        *Rt = Rs + SignedImm;
    } break;
    case 012: /* slti */
    {
        *Rt = (i32)Rs < (i32)SignedImm;
    } break;
    case 013: /* sltiu */
    {
        *Rt = Rs < SignedImm;
    } break;
    case 014: /* andi */
    {
        *Rt = Rs & UnsignedImm;
    } break;
    case 015: /* ori */
    {
        *Rt = Rs | UnsignedImm;
    } break;
    case 016: /* xori */
    {
        *Rt = Rs ^ UnsignedImm;
    } break;
    case 017: /* lui */
    {
        *Rt = UnsignedImm << 16;
    } break;

    default: Rt = NULL; return false;
    }

    if (Rt)
        R3051_ScheduleWriteback(This, Rt, *Rt, EXECUTE_STAGE);
    return false;
}


/* =============================================================================================
 *                                         MEMORY STAGE
 *=============================================================================================*/

/* returns true if an exception has occured during this stage, false otherwise */
static Bool8 R3051_Memory(R3051 *This)
{
    u32 Instruction = R3051_InstructionAt(This, MEMORY_STAGE);
    u32 Addr;
    {
        i32 Offset = (i32)(i16)(Instruction & 0xFFFF);
        u32 Base = This->R[REG(Instruction, RS)];
        Addr = Base + Offset;
    }
    u32 *Rt = &This->R[REG(Instruction, RT)];
    u32 DataRead = 0;

    /* NOTE: mem stage does not perform writeback to rt immediately, 
     * it leaves the content of rt intact until the writeback stage, 
     * this is different from the decode and execute stage because 
     * decode happens before execute */
    switch (OP(Instruction))
    {
    case 040: /* lb */
    {
        DataRead = (i32)(i8)READ_BYTE(Addr);
    } break;
    case 044: /* lbu */
    {
        DataRead = READ_BYTE(Addr);
    } break;

    case 041: /* lh */
    {
        if (Addr & 1)
            goto AddrErrorException;
        DataRead = (i32)(i16)READ_HALF(Addr);
    } break;
    case 045: /* lhu */
    {
        if (Addr & 1)
            goto AddrErrorException;
        DataRead = READ_HALF(Addr);
    } break;

    case 043: /* lw */
    {
        if (Addr & 3)
            goto AddrErrorException;
        DataRead = READ_WORD(Addr);
    } break;


    case 050: /* sb */
    {
        WRITE_BYTE(Addr, *Rt & 0xFF);
        Rt = NULL;
    } break;
    case 051: /* sh */
    {
        if (Addr & 1)
            goto AddrErrorException;
        WRITE_HALF(Addr, *Rt & 0xFFFF);
        Rt = NULL;
    } break;
    case 053: /* sw */
    {
        if (Addr & 3)
            goto AddrErrorException;
        WRITE_WORD(Addr, *Rt);
        Rt = NULL;
    } break;


    case 042: /* lwl */
    {
        /* reads 'up' from a given addr until it hits an addr that's divisble by 4 */
        u32 Data = 0;
        u32 Mask = 0;
        int BytesRead = 0;
        do {
            Mask = (Mask << 8) | 0x000000FF; /* shift in the byte mask */
            Data |= (u32)READ_BYTE(Addr) << BytesRead*8;
            BytesRead++;
        } while (++Addr & 0x3);
        DataRead = *Rt;
        MASKED_LOAD(DataRead, Data, Mask);
    } break;
    case 046: /* lwr */
    {
        /* reads 'down' from a given addr to one that's divisible by 4 */
        u32 Data = 0;
        u32 Mask = 0;
        int BytesRead = 0;
        do {
            Mask = (Mask >> 8) | 0xFF000000;
            Data |= (u32)READ_BYTE(Addr) << (3 - BytesRead)*8;
            BytesRead++;
        } while (Addr-- & 0x3);
        DataRead = *Rt;
        MASKED_LOAD(DataRead, Data, Mask);
    } break;
    case 052: /* swl */
    {
        /* stores 'up' from a given addr to one that's divisible by 4 */
        u32 Src = *Rt;
        do {
            WRITE_BYTE(Addr, Src & 0xFF);
            Src >>= 8;
        } while (Addr++ & 0x3);
        Rt = NULL;
    } break;
    case 056: /* swr */
    {
        /* stores 'down' from a given addr + 4 to one that's divisible by 4 */
        u32 Src = *Rt;
        Addr += 4;
        do {
            WRITE_BYTE(Addr, (Src >> 24) & 0xFF);
            Src <<= 8;
        } while (--Addr & 0x3);
        Rt = NULL;
    } break;

    default: return false;
    }

    R3051_ScheduleWriteback(This, Rt, DataRead, MEMORY_STAGE);
    return false;

AddrErrorException:
    return true;
}


/* =============================================================================================
 *                                         WRITEBACK STAGE
 *=============================================================================================*/

static void R3051_Writeback(R3051 *This)
{
    /* NOTE: writeback only reads hi and lo, execute stage writes to hi and lo, 
     * that's **probably** 
     * the reason by one must wait 2 instructions to write to hi/lo after reading them to avoid curruption
     *
     * https://devblogs.microsoft.com/oldnewthing/20180404-00/?p=98435
     */

    u32 Instruction = R3051_InstructionAt(This, WRITEBACK_STAGE);
    switch (OP(Instruction) | FUNCT(Instruction))
    {
    case 0x00000010: /* mfhi */
    {
        if (This->HiLoCyclesLeft)
        {
            This->HiLoBlocking = true;
            break;
        }
        This->R[REG(Instruction, RD)] = This->Hi;
        This->R[0] = 0;
    } break;
    case 0x00000012: /* mflo */
    {
        if (This->HiLoCyclesLeft)
        {
            This->HiLoBlocking = true;
            break;
        }
        This->R[REG(Instruction, RD)] = This->Lo;
        This->R[0] = 0;
    } break;
    }

    int CurrentPipeStage = R3051_CurrentPipeStage(This, WRITEBACK_STAGE);
    if (This->WritebackBuffer[CurrentPipeStage].RegRef)
    {
        *This->WritebackBuffer[CurrentPipeStage].RegRef = This->WritebackBuffer[CurrentPipeStage].Data;
        This->WritebackBuffer[CurrentPipeStage].RegRef = NULL;
    }
}






void R3051_StepClock(R3051 *This)
{
#define INVALIDATE_PIPELINE_IF(Cond, CurrentStageAndBelow) do {\
    if (Cond) {\
        StageToInvalidate = CurrentStageAndBelow;\
        goto InvalidatePipeline;\
    }\
} while (0)

    if (This->HiLoCyclesLeft)
    {
        This->HiLoCyclesLeft--;
        if (This->HiLoBlocking)
        {
            if (This->HiLoCyclesLeft == 0)
                This->HiLoBlocking = false;
            return;
        }
    }


    int StageToInvalidate; /* leave it unitialize will make the compiler emit better warnings */
    Bool8 HasException;
    /*
     * NOTE: because Mips is pipelined, some exceptions can occur early on in the pipeline than others, 
     * but Mips guarantees that exceptions are in instruction order, not pipeline order 
     */
    if (This->ExceptionRaised)
    {
        /* TODO: modify pipeline correctly */
        This->PipeStage = This->ExceptionCyclesLeft;
        if (0 == This->ExceptionCyclesLeft)
        {
            This->ExceptionRaised = false;
            /* TODO: execute exception handler routine */
        }
        else 
        {
            /* fallthrough is because mips is pipelined */
            switch (R3051_PIPESTAGE_COUNT - This->ExceptionCyclesLeft)
            {
            case WRITEBACK_STAGE:
            {
                R3051_Writeback(This);
            } FALL_THROUGH;
            case MEMORY_STAGE:
            {
                HasException = R3051_Memory(This);
                INVALIDATE_PIPELINE_IF(HasException, WRITEBACK_STAGE);
            } FALL_THROUGH;
            case EXECUTE_STAGE:
            {
                HasException = R3051_Execute(This);
                INVALIDATE_PIPELINE_IF(HasException, MEMORY_STAGE);
            } FALL_THROUGH;
            case DECODE_STAGE:      
            {
                HasException = R3051_Decode(This);
                INVALIDATE_PIPELINE_IF(HasException, EXECUTE_STAGE);
            } break;
            }

            This->ExceptionCyclesLeft--;
            return;
        }
    }


    R3051_Fetch(This);
    R3051_Writeback(This);

    HasException = R3051_Memory(This);
    INVALIDATE_PIPELINE_IF(HasException, EXECUTE_STAGE);

    HasException = R3051_Execute(This);
    INVALIDATE_PIPELINE_IF(HasException, DECODE_STAGE);

    HasException = R3051_Decode(This);
    INVALIDATE_PIPELINE_IF(HasException, FETCH_STAGE);

 
    This->PipeStage++;
    if (This->PipeStage >= R3051_PIPESTAGE_COUNT)
        This->PipeStage = 0;   
    return;


InvalidatePipeline:
    while (StageToInvalidate >= FETCH_STAGE)
    {
        int Stage = R3051_CurrentPipeStage(This, StageToInvalidate);
        This->PCSave[Stage] = 0;
        This->Instruction[Stage] = 0;
        StageToInvalidate--;
    }
#undef INVALIDATE_PIPELINE_IF
}


#undef FETCH_STAGE
#undef DECODE_STAGE
#undef EXECUTE_STAGE
#undef MEMORY_STAGE
#undef WRITEBACK_STAGE
#undef OVERFLOW_I32

#undef READ_BYTE
#undef READ_HALF
#undef READ_WORD
#undef WRITE_BYTE
#undef WRITE_HALF
#undef WRITE_WORD






#ifdef STANDALONE
#undef STANDALONE
#include <stdio.h>
#include <stdlib.h>

#include "Disassembler.c"


typedef struct Buffer 
{
    u8 *Ptr;
    iSize Size;
} Buffer;

static u8 *ReadBinaryFile(const char *FileName, iSize *OutFileSize)
{
    FILE *f = fopen(FileName, "rb");
    if (NULL == f)
    {
        perror(FileName);
        return NULL;
    }


    fseek(f, 0, SEEK_END);
    *OutFileSize = ftell(f);
    fseek(f, 0, SEEK_SET);


    u8 *Buffer = malloc(*OutFileSize);
    if (*OutFileSize != (iSize)fread(Buffer, 1, *OutFileSize, f))
    {
        perror("Unable to fully read file.");
        free(Buffer);
        Buffer = NULL;
    }
    fclose(f);
    return Buffer;
}


static u32 TranslateAddr(u32 LogicalAddr)
{
    u32 PhysicalAddr = 0;
    if (LogicalAddr < 512 * KB)
    {
        PhysicalAddr = LogicalAddr;
    }
    if (IN_RANGE(0xBFC00000, LogicalAddr, 0xBFC00000 + 512*KB))
    {
        PhysicalAddr = LogicalAddr - 0xBFC00000;
    }
    else return LogicalAddr;
    return PhysicalAddr;
}


static void MipsWrite(void *UserData, u32 Addr, u32 Data, R3051_DataSize Size)
{
    Addr = TranslateAddr(Addr);
    printf("WRITING: [%08x] <- %0*x\n", Addr, Size*2, (u32)(Data & (((u64)1 << Size*8) - 1)));
}

static u32 MipsRead(void *UserData, u32 Addr, R3051_DataSize Size)
{
    Buffer *Buf = UserData;
    Addr = TranslateAddr(Addr);
    if (Addr + Size > Buf->Size)
        return 0;

    u32 Data = 0;
    for (int i = 0; i < Size; i++)
    {
        Data |= (u32)Buf->Ptr[Addr + i] << i*8;
    }
    return Data;
}


static Bool8 ProcessCLI(R3051 *Mips)
{
    (void)Mips;
    char Input = 0;
    char InputBuffer[64];
    if (NULL == fgets(InputBuffer, sizeof InputBuffer, stdin))
        return false;
    Input = InputBuffer[0];

    return 'q' != Input;
}

static void DumpState(const R3051 *Mips)
{
    static R3051 LastState;
    /* dump regs */
    printf("========== Registers =========:\n");
    for (int i = 0; i < R3051_REG_COUNT; i++)
    {
        if (Mips->R[i] == LastState.R[i])
            printf(" R%02d %08x ", i, Mips->R[i]);
        else
            printf("[R%02d=%08x]", i, Mips->R[i]);

        if ((i - 3) % 4 == 0)
            printf("\n");
    }

    /* dump pipeline */
    char DisassembledInstruction[64];
    Bool8 AlreadyDecoded = false, 
          AlreadyExecuted = false;
    printf("========== Pipeline =========:\n");
    for (int i = 0; i < R3051_PIPESTAGE_COUNT; i++)
    {
        R3051_Disasm(
            Mips->Instruction[i], 
            Mips->PCSave[i], 
            DISASM_IMM16_AS_HEX, 
            DisassembledInstruction, 
            sizeof(DisassembledInstruction)
        );

        const char *StagePtr = "   ";
        if (Mips->PC - 4 == Mips->PCSave[i])
            StagePtr = "fi>";
        else if (Mips->PC - 4*(2) == Mips->PCSave[i] && !AlreadyDecoded)
        {
            StagePtr = "di ";
            AlreadyDecoded = true;
        }
        else if (Mips->PC - 4*3 == Mips->PCSave[i] && !AlreadyExecuted)
        {
            StagePtr = "ex ";
            AlreadyExecuted = true;
        }

        if (LastState.Instruction[i] == Mips->Instruction[i])
        {
            printf("%s    PC=%08x  %08x: %s  \n", 
                StagePtr, 
                Mips->PCSave[i], Mips->Instruction[i], 
                DisassembledInstruction
            );
        }
        else
        {
            printf("%s | [PC=%08x]:%08x: %s  \n", 
                StagePtr, 
                Mips->PCSave[i], Mips->Instruction[i], 
                DisassembledInstruction
            );
        }
        
    }
    printf("PC: %08x\n", Mips->PC);
    LastState = *Mips;
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("Usage: %s <mips binary file>\n", argv[0]);
        return 1;
    }
    Bool8 CLIDisable = argc == 3;

    Buffer Buf;
    Buf.Ptr = ReadBinaryFile(argv[1], &Buf.Size);
    if (NULL == Buf.Ptr)
        return 1;

    R3051 Mips = R3051_Init(
        &Buf, 
        MipsRead, MipsWrite
    );

    Bool8 ShouldContinue = true;
    do {
        R3051_StepClock(&Mips);
        DumpState(&Mips);
        if (!CLIDisable)
        {
            ShouldContinue = ProcessCLI(&Mips);
        }
    } while (ShouldContinue);

    free(Buf.Ptr);
    return 0;

}
#endif /* STANDALONE */

#endif /* R3051_C */

