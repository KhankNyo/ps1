#ifndef R3051_C
#define R3051_C

#include "Common.h"

typedef enum R3051_DataSize 
{
    DATA_BYTE = sizeof(u8),
    DATA_HALF = sizeof(u16),
    DATA_WORD = sizeof(u32),
} R3051_DataSize;


typedef enum R3051_Exception 
{
    EXCEPTION_INT = 0, /* INTerrrupt */
    /* NOTE: 1..3 are TLB exception, 
     * but not immplemented because the PS1 does not have virtual addr */
    EXCEPTION_ADEL = 4, /* Address Error on Load */
    EXCEPTION_ADES, /* Address Error on Store */
    EXCEPTION_IBE,  /* Instruction Bus Error */
    EXCEPTION_DBE,  /* Data Bus Error */
    EXCEPTION_SYS,  /* SYScall */
    EXCEPTION_BP,   /* BreakPoint */
    EXCEPTION_RI,   /* Reserved Instruction */
    EXCEPTION_CPU,  /* CoProcessor Unusable */
    EXCEPTION_OVF,  /* arithmetic OVerFlow */
} R3051_Exception;


typedef union R3051CP0
{
    u32 R[32];
    struct {
        u32 Reserved0[3];
        u32 BPC;                /* breakpoint on execute */

        u32 Reserved1;
        u32 BDA;                /* breakpoint on data access */
        u32 JumpDest;           /* random memorized jump addr */
        u32 DCIC;               /* breakpoint ctrl */

        u32 BadVAddr;           /* bad virtual addr */
        u32 BDAM;               /* data access breakpoint mask */
        u32 Reserved2;
        u32 BPCM;               /* execute breakpoint mask */

        u32 Status;
        u32 Cause;
        u32 EPC;
        u32 PrID;
    };
} R3051CP0;



#define R3051_RESET_VEC 0xBFC00000
#define R3051_PIPESTAGE_COUNT 5
typedef void (*R3051Write)(void *UserData, u32 Addr, u32 Data, R3051_DataSize Size);
typedef u32 (*R3051Read)(void *UserData, u32 Addr, R3051_DataSize Size);
typedef Bool8 (*R3051AddrVerifyFn)(void *UserData, u32 Addr);
typedef struct R3051 
{
    u32 R[32];
    u32 Hi, Lo;

    u32 PC;
    u32 PCSave[R3051_PIPESTAGE_COUNT];
    u32 Instruction[R3051_PIPESTAGE_COUNT];
    Bool8 InstructionIsBranch[R3051_PIPESTAGE_COUNT];
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
    R3051AddrVerifyFn VerifyInstructionAddr;
    R3051AddrVerifyFn VerifyDataAddr;


    R3051CP0 CP0;
} R3051;

R3051 R3051_Init(
    void *UserData, 
    R3051Read ReadFn, R3051Write WriteFn,
    R3051AddrVerifyFn DataAddrVerifier, R3051AddrVerifyFn InstructionAddrVerifier
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
    R3051Read ReadFn, R3051Write WriteFn,
    R3051AddrVerifyFn DataAddrVerifier, R3051AddrVerifyFn InstructionAddrVerifier
)
{
    u32 ResetVector = R3051_RESET_VEC;
    R3051 Mips = {
        .PC = ResetVector,
        .PCSave = { 0 },
        .Instruction = { 0 },
        .R = { 0 },

        .UserData = UserData,
        .WriteFn = WriteFn,
        .ReadFn = ReadFn,
        .VerifyDataAddr = DataAddrVerifier,
        .VerifyInstructionAddr = InstructionAddrVerifier,

        .CP0 = {
            .PrID = 0x00000001,
            .Status = 1 << 22, /* set BEV bit */
        },
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
    int CurrentPipeStage = R3051_CurrentPipeStage(This, Stage);
    if (Location && &This->R[0] != Location)
    {
        This->WritebackBuffer[CurrentPipeStage].RegRef = Location;
        This->WritebackBuffer[CurrentPipeStage].Data = Data;
    }
}




static void R3051_SetException(R3051 *This, R3051_Exception Exception, int Stage)
{
    int LastStage = Stage - 1;
    if (LastStage < 0)
        LastStage = R3051_PIPESTAGE_COUNT - 1;

    /* set EPC */
    if (This->InstructionIsBranch[LastStage])
    {
        This->CP0.EPC = This->PCSave[LastStage];
        This->CP0.Cause |= 1ul << 31; /* set BD bit */
    }
    else
    {
        This->CP0.EPC = This->PCSave[Stage];
    }

    /* write exception code to Cause register */
    MASKED_LOAD(This->CP0.Cause, Exception << 1, 0x1F << 1);

    /* pushes new kernel mode and interrupt flag
     * bit 1 = 0 for kernel mode, 
     * bit 0 = 0 for interrupt disable */
    u32 NewStatusStack = (This->CP0.Status & 0xF) << 2 | 0x00;
    MASKED_LOAD(This->CP0.Status, NewStatusStack, 0x3F);
}


static void R3051_RaiseInternalException(R3051 *This, R3051_Exception Exception, int Stage)
{
    This->ExceptionRaised = true;
    R3051_SetException(This, Exception, Stage);
}

static void R3051_RaiseMemoryException(R3051 *This, R3051_Exception Exception, u32 Addr)
{
    int Stage = R3051_CurrentPipeStage(This, MEMORY_STAGE);
    This->ExceptionRaised = true;
    This->CP0.BadVAddr = Addr;
    R3051_SetException(This, Exception, Stage);
}

/* sets PC to the appropriate exception routine, 
 * sets CP0 to the appriate state for the exception, 
 * caller decides whether or not to start fetching from that addr */
static void R3051_HandleException(R3051 *This)
{
    ASSERT(This->ExceptionRaised);
    ASSERT(This->ExceptionCyclesLeft == 0);
    This->ExceptionRaised = false;
    /* General exception vector */
    This->PC = 0x80000080;
}


static u32 R3051CP0_Read(const R3051 *This, uint RegIndex)
{
    switch (RegIndex)
    {
    case 3: return This->CP0.BPC;
    case 5: return This->CP0.BDA;
    case 6: return This->CP0.JumpDest;
    case 8: return This->CP0.BadVAddr;
    case 9: return This->CP0.BDAM;
    case 11: return This->CP0.BPCM;
    case 12: return This->CP0.Status;
    case 13: return This->CP0.Cause;
    case 14: return This->CP0.EPC;
    case 15: return 0x00000001; /* PrID, always 1 */
    default: return This->CP0.R[RegIndex];
    }
}

static void R3051CP0_Write(R3051 *This, u32 RdIndex, u32 Data)
{
    This->CP0.R[RdIndex % STATIC_ARRAY_SIZE(This->CP0.R)] = Data;
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



static Bool8 R3051_DecodeSpecial(R3051 *This, u32 Instruction, Bool8 *ExceptionRaisedThisStage, Bool8 *IsBranchingInstruction)
{
    int CurrentStage = R3051_CurrentPipeStage(This, DECODE_STAGE);
    Bool8 IsValidInstruction = true;
    *ExceptionRaisedThisStage = false;
    *IsBranchingInstruction = false;

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
            *IsBranchingInstruction = true;
        } break;
        case 1: /* jalr */
        {
            u32 Rs = This->R[REG(Instruction, RS)];
            u32 *Rd = &This->R[REG(Instruction, RD)];

            Bool8 TargetAddrIsValid = This->VerifyInstructionAddr(This->UserData, Rs); /* TODO: check this? */
            if ((Rs & 0x3) || !TargetAddrIsValid)
            {
                R3051_RaiseInternalException(
                    This, 
                    EXCEPTION_ADEL, 
                    CurrentStage
                );
                *ExceptionRaisedThisStage = true;
            }
            else
            {
                *Rd = This->PC;
                This->PC = Rs;
                R3051_ScheduleWriteback(This, Rd, *Rd, DECODE_STAGE);
            }
            *IsBranchingInstruction = true;
        } break;
        case 5: /* syscall */
        {
            R3051_RaiseInternalException(
                This, 
                EXCEPTION_SYS, 
                CurrentStage
            );
            *ExceptionRaisedThisStage = true;
        } break;
        case 6: /* break */
        {
            R3051_RaiseInternalException(
                This, 
                EXCEPTION_BP, 
                CurrentStage
            );
            *ExceptionRaisedThisStage = true;
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
    int CurrentStage = R3051_CurrentPipeStage(This, DECODE_STAGE);
    u32 Rs = This->R[REG(Instruction, RS)];
    u32 Rt = This->R[REG(Instruction, RT)];
    Bool8 InstructionIsIllegal = false;
    Bool8 IsBranchingInstruction = false;
    Bool8 ExceptionRaisedThisStage = false;

    switch (OP(Instruction))
    {
    case 000: /* special */
    {
        InstructionIsIllegal = !R3051_DecodeSpecial(
            This, 
            Instruction, 
            &ExceptionRaisedThisStage, 
            &IsBranchingInstruction
        );
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
        } break;
        case 0x11: /* bgezal */
        {
            ShouldBranch = (i32)Rs >= 0; 
            This->R[31] = NextInstructionAddr;
        } break;
        }

        BRANCH_IF(ShouldBranch);
        IsBranchingInstruction = true;
    } break;

    case 003: /* jal (jump and link) */
    {
        This->R[31] = NextInstructionAddr;
        goto j; /* goto is used instead of fallthrough to suppress warning */
    }
    case 002: /* j (jump) */
j:
    {
        /* NOTE: these j(al) instructions don't check for validity of target addr unlike jr and jalr */
        u32 AddrMask = 0x03FFFFFF;
        u32 Immediate = (Instruction & AddrMask) << 2;
        This->PC = (This->PC & 0xF0000000) | Immediate;
        IsBranchingInstruction = true;
    } break;

    case 004: /* beq */  BRANCH_IF(Rs == Rt); IsBranchingInstruction = true; break;
    case 005: /* bne */  BRANCH_IF(Rs != Rt); IsBranchingInstruction = true; break;
    case 006: /* blez */ BRANCH_IF((i32)Rs <= 0); IsBranchingInstruction = true; break;
    case 007: /* bgtz */ BRANCH_IF((i32)Rs > 0); IsBranchingInstruction = true; break;

    default: 
    {
        u32 OpMode = OP_MODE(Instruction);
        switch (OP_GROUP(Instruction))
        {
        case 06: /* coprocessor load group */
        case 07: /* coprocessor store group */
        {
            if (OpMode == 2) /* TODO: check if CP2 is usable */
            {
                /* instruction is ok */
            }
            else if (OpMode == 3) /* CP3 always triggers illegal instruction */
            {
                InstructionIsIllegal = true;
            }
            else /* other coprocessor triggers coprocessor unusable */
            {
                R3051_RaiseInternalException(This, EXCEPTION_CPU, CurrentStage);
            }
        } break;
        case 02: /* coprocessor misc group */
        {
            if (OpMode == 0) /* CP0 */
            {
                Bool8 IsRFE = (REG(Instruction, RS) & 0x10) && 0x10 == FUNCT(Instruction);
                Bool8 IsMFC0 = (REG(Instruction, RS) & ~1u) == 00;
                Bool8 IsMTC0 = (REG(Instruction, RS) & ~1u) == 04;
                InstructionIsIllegal = !(IsRFE || IsMFC0 || IsMTC0);
            }
            else if (OpMode != 2) /* CP2 (GTE) */
            {
                /* TODO: check GTE */
                InstructionIsIllegal = true;
            }
        } break;

        case 03: InstructionIsIllegal = true; break; /* illegal */
        case 04: InstructionIsIllegal = OpMode == 07; break; /* load group */
        case 05: InstructionIsIllegal = OpMode > 3 && OpMode != 6; break; /* store group */
        }
    } break;
    }

    if (InstructionIsIllegal)
    {
        R3051_RaiseInternalException(
            This, 
            EXCEPTION_RI, 
            CurrentStage
        );
        ExceptionRaisedThisStage = true;
    }
    This->InstructionIsBranch[CurrentStage] = IsBranchingInstruction;
    return ExceptionRaisedThisStage;
#undef BRANCH_IF 
}


static u32 *R3051_ExecuteSpecial(R3051 *This, u32 Instruction, u32 Rt, u32 Rs, Bool8 *ExceptionRaisedThisStage)
{
    *ExceptionRaisedThisStage = false;
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
            R3051_RaiseInternalException(
                This, 
                EXCEPTION_OVF, 
                R3051_CurrentPipeStage(This, EXECUTE_STAGE)
            );
            *ExceptionRaisedThisStage = true;
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
            R3051_RaiseInternalException(
                This, 
                EXCEPTION_OVF, 
                R3051_CurrentPipeStage(This, EXECUTE_STAGE)
            );
            *ExceptionRaisedThisStage = true;
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

    Bool8 ExceptionRaisedThisStage = false;

    /* only care about ALU ops */
    switch (OP(Instruction)) /* group and mode of op */
    {
    case 000:
    {
        Rt = R3051_ExecuteSpecial(This, Instruction, *Rt, Rs, &ExceptionRaisedThisStage);
    } break;

    /* group 1: alu immediate */
    case 010: /* addi */
    {
        u32 Result = Rs + SignedImm;
        if (OVERFLOW_I32(Result, Rs, SignedImm))
        {
            R3051_RaiseInternalException(
                This, 
                EXCEPTION_OVF, 
                R3051_CurrentPipeStage(This, EXECUTE_STAGE)
            );
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

    case 020: /* COP0 */
    {
        /* RFE, the manual shows that the bits in the middle are zero, 
         * but does it really need to be zero?
         * does the opcode need to be 0x42000010 exactly? */
        if ((REG(Instruction, RS) & 0x10) && FUNCT(Instruction) == 0x10) 
        {
            /* restore KUp, IEp and KUc, IEc */
            MASKED_LOAD(This->CP0.Status, This->CP0.Status >> 2, 0xF);
            Rt = NULL;
        }
        else switch (REG(Instruction, RS))
        {
        case 0x00:
        case 0x01: /* MFC0 */
        {
            u32 CP0RegisterContent = R3051CP0_Read(This, REG(Instruction, RT));
            R3051_ScheduleWriteback(This, Rt, CP0RegisterContent, EXECUTE_STAGE);
            TODO("Check if coprocessor is usable for MFC0");
            Rt = NULL;
        } break;
        case 0x04:
        case 0x05: /* MTC0 */
        {
            R3051CP0_Write(This, REG(Instruction, RD), *Rt);
            TODO("Check if coprocessor is usable for MTC0");
            Rt = NULL;
        } break;
        }
    } break;
    case 022: /* COP2 */
    {
        /* TODO: COP2 instructions */
        switch (REG(Instruction, RS))
        {
        case 0x00:
        case 0x01:
        {
        } break;
        case 0x02:
        case 0x03:
        {
        } break;
        case 0x04:
        case 0x05:
        {
        } break;
        case 0x06:
        case 0x07:
        {
        } break;
        }
    } break;

    default: return false;
    }

    This->R[0] = 0;
    return ExceptionRaisedThisStage;
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

    /* verify memory addr */
    if (!This->VerifyDataAddr(This->UserData, Addr))
    {
        if ((OP(Instruction) & 070) == 4) /* load instruction */
            goto LoadAddrError;
        else goto StoreAddrError;
    }

    /* NOTE: load instructions do not perform writeback to rt immediately, 
     * it leaves the content of rt intact until the writeback stage */
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
            goto LoadAddrError;
        DataRead = (i32)(i16)READ_HALF(Addr);
    } break;
    case 045: /* lhu */
    {
        if (Addr & 1)
            goto LoadAddrError;
        DataRead = READ_HALF(Addr);
    } break;

    case 043: /* lw */
    {
        if (Addr & 3)
            goto LoadAddrError;
        DataRead = READ_WORD(Addr);
    } break;


    case 050: /* sb */
    {
        WRITE_BYTE(Addr, *Rt & 0xFF);
        return false;
    } break;
    case 051: /* sh */
    {
        if (Addr & 1)
            goto StoreAddrError;
        WRITE_HALF(Addr, *Rt & 0xFFFF);
        return false;
    } break;
    case 053: /* sw */
    {
        if (Addr & 3)
            goto StoreAddrError;
        WRITE_WORD(Addr, *Rt);
        return false;
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
        return false;
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
        return false;
    } break;

    default: return false;
    }

    R3051_ScheduleWriteback(This, Rt, DataRead, MEMORY_STAGE);
    return false;

LoadAddrError:
    R3051_RaiseMemoryException(This, EXCEPTION_ADEL, Addr);
    return true;
StoreAddrError:
    R3051_RaiseMemoryException(This, EXCEPTION_ADES, Addr);
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
    } break;
    case 0x00000012: /* mflo */
    {
        if (This->HiLoCyclesLeft)
        {
            This->HiLoBlocking = true;
            break;
        }
        This->R[REG(Instruction, RD)] = This->Lo;
    } break;
    }

    int CurrentPipeStage = R3051_CurrentPipeStage(This, WRITEBACK_STAGE);
    if (This->WritebackBuffer[CurrentPipeStage].RegRef)
    {
        *This->WritebackBuffer[CurrentPipeStage].RegRef = This->WritebackBuffer[CurrentPipeStage].Data;
        This->WritebackBuffer[CurrentPipeStage].RegRef = NULL;
        This->R[0] = 0;
    }
}





static void R3051_AdvancePipeStage(R3051 *This)
{
    This->PipeStage++;
    if (This->PipeStage >= R3051_PIPESTAGE_COUNT)
        This->PipeStage = 0;   
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
        if (This->HiLoBlocking) /* suspend execution if we're waiting for hi/lo result */
        {
            if (This->HiLoCyclesLeft == 0)
                This->HiLoBlocking = false;
            return;
        }
    }


    int StageToInvalidate;
    Bool8 HasException;
    R3051_AdvancePipeStage(This);
    if (This->ExceptionRaised)
    {
        if (This->ExceptionCyclesLeft) /* needs to empty all instruction in the pipeline */
        {
            /* don't do fetch when emptying out instructions in the pipeline, 
             * put nops in their place instead */

            int FetchStage = R3051_CurrentPipeStage(This, FETCH_STAGE);
            This->Instruction[FetchStage] = 0;
            This->ExceptionCyclesLeft--;
        }
        else /* emptied all instructions in the pipeline */
        {
            R3051_HandleException(This);
            /* starts fetching from the exception handler */
            R3051_Fetch(This);
        }
    }
    else /* no exception, normal fetching */
    {
        R3051_Fetch(This);
    }


    /*
     * NOTE: this weird pipeline ordering is because Mips exception has to follow instruction order, 
     * but because of Mip's pipelined nature, 
     * some instructions can trigger exception before instructions behind it can even execute.
     * So the solution is to execute the stages of earlier instructions first, 
     * so they can have a chance to trigger exceptions before the instructions after them.
     * This also helps with forwarding, especially the fact that execute stage can forward their result to the decode stage, 
     * e.g. an alu instruction followed by a branch/jump consuming the result of the alu instruction.
     */
    R3051_Writeback(This);

    HasException = R3051_Memory(This);
    INVALIDATE_PIPELINE_IF(HasException, EXECUTE_STAGE);

    HasException = R3051_Execute(This);
    INVALIDATE_PIPELINE_IF(HasException, DECODE_STAGE);

    HasException = R3051_Decode(This);
    INVALIDATE_PIPELINE_IF(HasException, FETCH_STAGE);
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
#include <string.h>

#include "Disassembler.c"


typedef enum TestSyscall 
{
    TESTSYS_WRITESTR    = 0x70000000,
    TESTSYS_CLRSCR      = 0x71000000,
    TESTSYS_EXIT        = 0x72000000,
} TestSyscall;

typedef struct Vec2i 
{
    int x, y;
} Vec2i;

typedef struct ScreenBuffer 
{
    char *Buffer;
    iSize BufferSizeBytes;
    int Width, Height, XOffset, YOffset;
    Vec2i WriteCursor;
} ScreenBuffer;

typedef struct Buffer 
{
    u8 *Ptr;
    iSize Size;
} Buffer;



#define STATUS_HEIGHT (24)
#define STATUS_WIDTH (14*4)
#define LOG_HEIGHT (8)
#define LOG_WIDTH STATUS_WIDTH
#define SEPARATOR_WIDTH 1
#define SEPARATOR_HEIGHT 1
#define TERM_HEIGHT (LOG_HEIGHT + STATUS_HEIGHT)
#define TERM_WIDTH  60
#define NEWLINE_WIDTH 1
#define SCREEN_WIDTH (STATUS_WIDTH + SEPARATOR_WIDTH + TERM_WIDTH + SEPARATOR_WIDTH + NEWLINE_WIDTH)
#define SCREEN_HEIGHT (2*SEPARATOR_HEIGHT + TERM_HEIGHT)

static Bool8 sShouldContinue;
static char sMasterScreenBuffer[SCREEN_HEIGHT*SCREEN_WIDTH + 1];
static ScreenBuffer sMasterWindow = {
    .Width = SCREEN_WIDTH,
    .Height = SCREEN_HEIGHT,
    .Buffer = sMasterScreenBuffer,
    .BufferSizeBytes = sizeof sMasterScreenBuffer
};
static ScreenBuffer sTerminalWindow = {
    .Buffer = sMasterScreenBuffer,
    .XOffset = STATUS_WIDTH + SEPARATOR_WIDTH,
    .YOffset = SEPARATOR_HEIGHT,
    .Width = TERM_WIDTH,
    .Height = TERM_HEIGHT,
    .BufferSizeBytes = sizeof sMasterScreenBuffer,
};
static ScreenBuffer sStatusWindow = {
    .Buffer = sMasterScreenBuffer,
    .XOffset = 0,
    .YOffset = SEPARATOR_HEIGHT,
    .Width = STATUS_WIDTH,
    .Height = STATUS_HEIGHT,
    .BufferSizeBytes = sizeof sMasterScreenBuffer,
};
static ScreenBuffer sLogWindow = {
    .Buffer = sMasterScreenBuffer,
    .XOffset = 0,
    .YOffset = STATUS_HEIGHT + SEPARATOR_HEIGHT,
    .Width = LOG_WIDTH,
    .Height = LOG_HEIGHT,
    .BufferSizeBytes = sizeof sMasterScreenBuffer
};



#define SCREEN_PRINTF(pScreen, uintFlags, ...) do {\
    char Buf[256];\
    snprintf(Buf, sizeof Buf, __VA_ARGS__);\
    ScreenWriteStr(pScreen, Buf, sizeof Buf, uintFlags);\
} while (0)

static char *ScreenGetWritePtr(ScreenBuffer *Screen, int x, int y)
{
    iSize LinearIndex = (y + Screen->YOffset)*SCREEN_WIDTH + x + Screen->XOffset;
    return Screen->Buffer + (LinearIndex % Screen->BufferSizeBytes);
}

static void ScreenMoveWriteCursorTo(ScreenBuffer *Screen, int x, int y)
{
    Screen->WriteCursor = (Vec2i) {
        .x = (x % Screen->Width), 
        .y = (y % Screen->Height),
    };
}

static void ScreenNewline(ScreenBuffer *Screen)
{
    ScreenMoveWriteCursorTo(Screen, 0, Screen->WriteCursor.y + 1);
}

#define WRAP_X (1 << 0)
#define WRAP_Y (1 << 1)
static uint ScreenIncPos(ScreenBuffer *Screen)
{
    uint WrapFlags = 0;
    Screen->WriteCursor.x++;
    if (Screen->WriteCursor.x == Screen->Width)
    {
        Screen->WriteCursor.x = 0;
        Screen->WriteCursor.y++;
        WrapFlags |= WRAP_X;
        if (Screen->WriteCursor.y == Screen->Height)
        {
            Screen->WriteCursor.y = 0;
            WrapFlags |= WRAP_Y;
        }
    }
    return WrapFlags;
}


static void ScreenWriteStr(ScreenBuffer *Screen, const char *Str, iSize MaxLen, uint Flags)
{
    while (MaxLen --> 0 && *Str)
    {
        char Ch = *Str++;
        /* write the char */
        switch (Ch)
        {
        default:
        {
            char *WritePtr = ScreenGetWritePtr(Screen, Screen->WriteCursor.x, Screen->WriteCursor.y);
            *WritePtr = Ch;

            uint Wrapped = ScreenIncPos(Screen);
            if (!(Flags & WRAP_X) && (Wrapped & WRAP_X))
                goto Out;
            if (!(Flags & WRAP_Y) && (Wrapped & WRAP_Y))
                goto Out;
        } break;
        case '\n':
        {
            ScreenNewline(Screen);
        } break;
        case '\r':
        {
            ScreenMoveWriteCursorTo(Screen, 0, Screen->WriteCursor.y - 1);
        } break;
        }
    }
Out:
    ;
}

static void ScreenClear(ScreenBuffer *Screen, char Ch)
{
    ScreenMoveWriteCursorTo(Screen, 0, 0);
    do {
        char *WritePtr = ScreenGetWritePtr(
            Screen, 
            Screen->WriteCursor.x, 
            Screen->WriteCursor.y
        );
        *WritePtr = Ch;
    } while (!(WRAP_Y & ScreenIncPos(Screen)));
    ScreenMoveWriteCursorTo(Screen, 0, 0);
}

static void ScreenInit(void)
{
    for (int x = 0; x < SCREEN_WIDTH; x++)
    {
        *ScreenGetWritePtr(&sMasterWindow, x, 0) = '=';
        *ScreenGetWritePtr(&sMasterWindow, x, SCREEN_HEIGHT - SEPARATOR_HEIGHT) = '=';
    }
    for (int y = 0; y < SCREEN_HEIGHT; y++)
    {
        *ScreenGetWritePtr(&sMasterWindow, SCREEN_WIDTH - NEWLINE_WIDTH, y) = '\n';
        *ScreenGetWritePtr(&sMasterWindow, SCREEN_WIDTH - NEWLINE_WIDTH - SEPARATOR_WIDTH, y) = '|';
        *ScreenGetWritePtr(&sMasterWindow, STATUS_WIDTH, y) = '|';
    }
    ScreenClear(&sStatusWindow, ' ');
    ScreenClear(&sTerminalWindow, ' ');
    ScreenClear(&sLogWindow, ' ');
    sMasterScreenBuffer[sizeof sMasterScreenBuffer - 1] = '\0';
}




static u8 *ReadBinaryFile(const char *FileName, iSize *OutFileSize, iSize ExtraSize)
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


    u8 *Buffer = malloc(*OutFileSize + ExtraSize);
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
    switch (LogicalAddr)
    {
    case TESTSYS_WRITESTR:
    case TESTSYS_EXIT:
    case TESTSYS_CLRSCR:
    {
        return LogicalAddr;
    } break;
    default: return LogicalAddr - R3051_RESET_VEC;
    }
}

static void MipsWrite(void *UserData, u32 Addr, u32 Data, R3051_DataSize Size)
{
    Buffer *Buf = UserData;
    Addr = TranslateAddr(Addr);
    if (Addr == (u32)TESTSYS_WRITESTR)
    {
        Data -= R3051_RESET_VEC;
        if (Data >= Buf->Size)
        {
            SCREEN_PRINTF(&sLogWindow, WRAP_X, 
                "Invalid string address: 0x%08x\n", 
                Data + R3051_RESET_VEC
            );
        }
        else
        {
            iSize MaxLength = Buf->Size - (&Buf->Ptr[Data] - Buf->Ptr);
            ScreenWriteStr(&sTerminalWindow, (const char *)&Buf->Ptr[Data], MaxLength, false);
        }
    }
    else if (Addr == (u32)TESTSYS_CLRSCR)
    {
        ScreenClear(&sTerminalWindow, ' ');
    }
    else if (Addr == (u32)TESTSYS_EXIT)
    {
        sShouldContinue = false;
    }
    else if (Addr + Size <= Buf->Size)
    {
        for (int i = 0; i < Size; i++)
        {
            Buf->Ptr[Addr + i] = Data & 0xFF;
            Data >>= 8;
        }
    }
    else
    {
        SCREEN_PRINTF(&sLogWindow, WRAP_X, 
            "Out of bound write to 0x%08x with %x (size %d)\n", 
            Addr + R3051_RESET_VEC, Data, Size
        );
    }
}

static u32 MipsRead(void *UserData, u32 Addr, R3051_DataSize Size)
{
    Buffer *Buf = UserData;
    Addr = TranslateAddr(Addr);
    if (Addr + Size <= Buf->Size)
    {
        u32 Data = 0;
        for (int i = 0; i < Size; i++)
        {
            Data |= (u32)Buf->Ptr[Addr + i] << 8*i;
        }
        return Data;
    }
    else
    {
        SCREEN_PRINTF(&sLogWindow, WRAP_X,
            "Out of bound read at 0x%08x, size = %d\n", 
            Addr + R3051_RESET_VEC, Size
        );
    }
    return 0;
}

static Bool8 MipsVerify(void *UserData, u32 Addr)
{
    return true;
}


static Bool8 ProcessCLI(R3051 *Mips)
{
    return true;
    (void)Mips;
    char Input = 0;
    char InputBuffer[64];
    if (NULL == fgets(InputBuffer, sizeof InputBuffer, stdin))
        return false;
    Input = InputBuffer[0];

    return 'q' != Input;
}

static void UpdateDisassembly(ScreenBuffer *Screen, const R3051 *Mips, int StartX, int StartY)
{
#define NUM_INS 10
    typedef struct DisasmInfo 
    {
        char Opc[64];
        u32 Instruction;
        u32 Addr;
    } DisasmInfo;

    static DisasmInfo Disasm[NUM_INS] = { 0 };
    if (!IN_RANGE(Disasm[0].Addr, Mips->PC, Disasm[NUM_INS - 1].Addr))
    {
        const Buffer *Buf = Mips->UserData;
        u32 CurrentPC = Mips->PC;
        for (int i = 0; i < NUM_INS; i++)
        {
            u32 PhysAddr = TranslateAddr(CurrentPC);
            if (PhysAddr + 4 <= Buf->Size)
            {
                u32 Instruction;
                memcpy(&Instruction, Buf->Ptr + PhysAddr, sizeof Instruction);
                R3051_Disasm(
                    Instruction, 
                    CurrentPC,
                    0,
                    Disasm[i].Opc, 
                    sizeof Disasm[i].Opc
                );
                Disasm[i].Instruction = Instruction;
                Disasm[i].Addr = CurrentPC;
                CurrentPC += 4;
            }
            else
            {
                Disasm[i].Addr = CurrentPC;
                Disasm[i].Instruction = 0;
                snprintf(Disasm[i].Opc, sizeof Disasm[i].Opc, "???");
            }
        }
    }

    ScreenMoveWriteCursorTo(Screen, StartX, StartY);
    for (int i = 0; i < NUM_INS; i++)
    {
        const char *Pointer = "   ";
        if (Mips->PC == Disasm[i].Addr)
            Pointer = "PC>";
        else if (Mips->PC - 4*sizeof(Disasm[0].Instruction) == Disasm[i].Addr)
            Pointer = "wb>";
        SCREEN_PRINTF(Screen, 0, 
            "%s %08X:  %08X  %s\n", 
            Pointer, Disasm[i].Addr, Disasm[i].Instruction, Disasm[i].Opc
        );
    }
#undef NUM_INS
}

static void DumpState(const R3051 *Mips)
{
    static R3051 LastState;
    int RegisterCount = STATIC_ARRAY_SIZE(Mips->R);
    int RegisterBoxPerLine = 4;
    int y = 0;

    /* dump regs */
    ScreenClear(&sStatusWindow, ' ');
    const char *Display = "========== Registers ==========";
    int DisplayX = (sStatusWindow.Width - strlen(Display)) / 2;
    ScreenMoveWriteCursorTo(&sStatusWindow, DisplayX, y);
    ScreenWriteStr(&sStatusWindow, Display, INT64_MAX, false);

    for (y = 1; y < 1 + RegisterCount / RegisterBoxPerLine; y++)
    {
        for (int x = 0; x < RegisterBoxPerLine; x++)
        {
            int StrLen = 14;
            int BoxPos = x*StrLen;

            int RegisterIndex = (y - 1)*RegisterBoxPerLine + x;
            ScreenMoveWriteCursorTo(&sStatusWindow, BoxPos, y);
            if (Mips->R[RegisterIndex] == LastState.R[RegisterIndex])
                SCREEN_PRINTF(&sStatusWindow, 0,  " R%02d %08x ", RegisterIndex, Mips->R[RegisterIndex]);
            else SCREEN_PRINTF(&sStatusWindow, 0, "[R%02d=%08x]", RegisterIndex, Mips->R[RegisterIndex]);
        }
    }

    Display = "========== Disassembly ==========";
    DisplayX = (sStatusWindow.Width - strlen(Display)) / 2;
    ScreenMoveWriteCursorTo(&sStatusWindow, DisplayX, y++);
    ScreenWriteStr(&sStatusWindow, Display, INT32_MAX, 0);

    UpdateDisassembly(&sStatusWindow, Mips, 0, y + 1);
    LastState = *Mips;

    printf("%s", sMasterScreenBuffer);
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
    Buf.Ptr = ReadBinaryFile(argv[1], &Buf.Size, 0);
    if (NULL == Buf.Ptr)
        return 1;

    ScreenInit();
    R3051 Mips = R3051_Init(
        &Buf, 
        MipsRead, MipsWrite,
        MipsVerify, MipsVerify
    );

    sShouldContinue = true;
    do {
        R3051_StepClock(&Mips);
        DumpState(&Mips);
        if (!CLIDisable)
        {
            sShouldContinue = sShouldContinue && ProcessCLI(&Mips);
        }
    } while (sShouldContinue);

    free(Buf.Ptr);
    return 0;
}
#endif /* STANDALONE */

#endif /* R3051_C */

