#ifndef R3000A_C
#define R3000A_C
/* =============================================================================================
 *
 *                                       IMPLEMENTATION 
 *
 *=============================================================================================*/

#include "CP0.h"
#include "R3000A.h"


/*
 * https://stuff.mit.edu/afs/sipb/contrib/doc/specs/ic/cpu/mips/r3051.pdf
 * INSTRUCTION SET ARCHITECTURE CHAPTER 2
 *      table 2.10: Opcode Encoding
 *
 * https://student.cs.uwaterloo.ca/~cs350/common/r3000-manual.pdf
 * MACHINE INSTRUCTION REFERENCE APPENDIX A
 */


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

typedef struct R3000A_StageStatus
{
    Bool8 HasException;
    u8 RegIndex;
    u32 WritebackData;
} R3000A_StageStatus;

typedef struct R3000A_ExceptionInfo 
{
    u32 EPC;
    u32 Instruction;
    Bool8 BranchDelay;
} R3000A_ExceptionInfo;



R3000A R3000A_Init(
    void *UserData, 
    R3000ARead ReadFn, 
    R3000AWrite WriteFn,
    R3000AAddrVerifyFn DataAddrVerifier, 
    R3000AAddrVerifyFn InstructionAddrVerifier
)
{
    R3000A Mips = {
        .PC = R3000A_RESET_VEC,

        .UserData = UserData,
        .WriteFn = WriteFn,
        .ReadFn = ReadFn,
        .VerifyDataAddr = DataAddrVerifier,
        .VerifyInstructionAddr = InstructionAddrVerifier,
    };
    Mips.CP0 = CP0_Init(0x00000001);
    return Mips;
}

void R3000A_Reset(R3000A *This)
{
    *This = R3000A_Init(
        This->UserData, 
        This->ReadFn, 
        This->WriteFn, 
        This->VerifyDataAddr, 
        This->VerifyInstructionAddr
    );
}


static int R3000A_CurrentPipeStage(const R3000A *This, int Stage)
{
    int CurrentStage = This->PipeStage - Stage;
    if (CurrentStage < 0)
        CurrentStage += R3000A_PIPESTAGE_COUNT;
    if (CurrentStage >= R3000A_PIPESTAGE_COUNT)
        CurrentStage -= R3000A_PIPESTAGE_COUNT;
    return CurrentStage;
}

static u32 R3000A_InstructionAt(const R3000A *This, int Stage)
{
    int CurrentStage = R3000A_CurrentPipeStage(This, Stage);
    return This->Instruction[CurrentStage];
}





static R3000A_ExceptionInfo R3000A_GetExceptionInfo(const R3000A *This, int Stage)
{
    int LastStage = Stage - 1;
    if (LastStage < 0)
        LastStage = R3000A_PIPESTAGE_COUNT - 1;

    R3000A_ExceptionInfo Info = {
        .EPC = This->PCSave[Stage],
        .Instruction = This->Instruction[Stage],
        .BranchDelay = This->InstructionIsBranch[LastStage],
    };
    if (Info.BranchDelay)
    {
        Info.EPC = This->PCSave[LastStage];
        Info.Instruction = This->Instruction[LastStage];
    }
    return Info;
}


static void R3000A_RaiseInternalException(R3000A *This, R3000A_Exception Exception, int Stage)
{
    This->ExceptionRaised = true;

    R3000A_ExceptionInfo Info = R3000A_GetExceptionInfo(This, Stage);
    CP0_SetException(
        &This->CP0,
        Exception,
        Info.EPC, 
        Info.Instruction, 
        Info.BranchDelay,
        0
    );
}

static void R3000A_RaiseMemoryException(R3000A *This, R3000A_Exception Exception, u32 Addr)
{
    int Stage = R3000A_CurrentPipeStage(This, MEMORY_STAGE);
    This->ExceptionRaised = true;

    R3000A_ExceptionInfo Info = R3000A_GetExceptionInfo(This, Stage);
    CP0_SetException(
        &This->CP0,
        Exception, 
        Info.EPC, 
        Info.Instruction, 
        Info.BranchDelay, 
        Addr
    );
}

/* sets PC to the appropriate exception routine, 
 * sets CP0 to the appriate state for the exception, 
 * caller decides whether or not to start fetching from that addr */
static void R3000A_HandleException(R3000A *This)
{
    ASSERT(This->ExceptionRaised);
    ASSERT(This->ExceptionCyclesLeft == 0);

    This->ExceptionRaised = false;
    This->PC = CP0_GetExceptionVector(&This->CP0) - 4;
}





/* =============================================================================================
 *                                         FETCH STAGE
 *=============================================================================================*/


static void R3000A_Fetch(R3000A *This)
{
    This->PCSave[This->PipeStage] = This->PC;
    This->Instruction[This->PipeStage] = READ_WORD(This->PC);
    This->PC += sizeof(u32);
}



static Bool8 R3000A_DecodeSpecial(R3000A *This, u32 Instruction, Bool8 *ExceptionRaisedThisStage, Bool8 *IsBranchingInstruction)
{
    int CurrentStage = R3000A_CurrentPipeStage(This, DECODE_STAGE);
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

            Bool8 TargetAddrIsValid = This->VerifyInstructionAddr(This->UserData, Rs);
            if ((Rs & 0x3) || !TargetAddrIsValid)
            {
                R3000A_RaiseInternalException(
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
            }
            *IsBranchingInstruction = true;
        } break;
        case 5: /* syscall */
        {
            R3000A_RaiseInternalException(
                This, 
                EXCEPTION_SYS, 
                CurrentStage
            );
            *ExceptionRaisedThisStage = true;
        } break;
        case 6: /* break */
        {
            R3000A_RaiseInternalException(
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
static Bool8 R3000A_Decode(R3000A *This)
{
#define BRANCH_IF(Cond) \
    if (Cond) \
        This->PC = This->PC - 4 + ((i32)(i16)(Instruction & 0xFFFF) << 2)

    /* NOTE: decode stage determines next PC value and checks for illegal instruction  */
    u32 NextInstructionAddr = This->PC;
    u32 Instruction = R3000A_InstructionAt(This, DECODE_STAGE);
    int CurrentStage = R3000A_CurrentPipeStage(This, DECODE_STAGE);
    u32 Rs = This->R[REG(Instruction, RS)];
    u32 Rt = This->R[REG(Instruction, RT)];
    Bool8 InstructionIsIllegal = false;
    Bool8 IsBranchingInstruction = false;
    Bool8 ExceptionRaisedThisStage = false;

    switch (OP(Instruction))
    {
    case 000: /* special */
    {
        InstructionIsIllegal = !R3000A_DecodeSpecial(
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
        Bool8 CoprocessorUnusable = false;
        u32 OpMode = OP_MODE(Instruction);
        switch (OP_GROUP(Instruction))
        {
        case 06: /* coprocessor load group */
        case 07: /* coprocessor store group */
        {
            if (OpMode == 2) /* GTE */
            {
                /* instruction is ok */
                CoprocessorUnusable = CP0_IsCoprocessorAvailable(&This->CP0, 2);
            }
            else if (OpMode == 3) /* CP3 always triggers illegal instruction exception instead of unusable exception */
            {
                InstructionIsIllegal = true;
            }
            else /* other coprocessor triggers coprocessor unusable */
            {
                CoprocessorUnusable = true;
            }

            if (CoprocessorUnusable)
                R3000A_RaiseInternalException(This, EXCEPTION_CPU, CurrentStage);
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
                InstructionIsIllegal = true;
                TODO("Check legality of GTE instructions");
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
        R3000A_RaiseInternalException(
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



/* returns true if exception was raised during this stage, false otherwise */
static R3000A_StageStatus R3000A_ExecuteSpecial(R3000A *This, u32 Instruction, u32 Rt, u32 Rs)
{
    R3000A_StageStatus Status = { 0 };
    uint RegIndex = REG(Instruction, RD);
    u32 *Rd = &This->R[RegIndex];

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
            (u32)~(~(i32)Rt >> SHAMT(Instruction))
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
        *Rd = Rt >> Rs;
    } break;
    case 007: /* srav */
    {
        Rs &= 0x1F;
        *Rd = Rt & 0x80000000?
            (u32)~(~(i32)Rt >> Rs) 
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
        return Status;
    } break;
    case 023: /* mtlo */
    {
        This->Lo = Rs;
        return Status;
    } break;
    case 020: /* mfhi */
    {
        if (This->HiLoCyclesLeft)
            This->HiLoBlocking = true;
        else This->R[REG(Instruction, RD)] = This->Hi;
    } break;
    case 022: /* mflo */
    {
        if (This->HiLoCyclesLeft)
            This->HiLoBlocking = true;
        else This->R[REG(Instruction, RD)] = This->Lo;
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
        return Status;
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
        return Status;
    } break;
    case 032: /* div */
    {
        This->HiLoCyclesLeft = 36;
        i32 SignedRt = (i32)Rt;
        i32 SignedRs = (i32)Rs;
        if (SignedRt == 0)
        {
            /* NOTE: returns 1 if sign bit is set, otherwise return -1 */
            This->Lo = SignedRs < 0?
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
            This->Lo = SignedRs / SignedRt;
            This->Hi = SignedRs % SignedRt;
        }
        return Status;
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
        return Status;
    } break;


    /* group 4: alu */
    case 040: /* add */
    {
        u32 Result = Rs + Rt;
        if (OVERFLOW_I32(Result, Rs, Rt))
        {
            R3000A_RaiseInternalException(
                This, 
                EXCEPTION_OVF, 
                R3000A_CurrentPipeStage(This, EXECUTE_STAGE)
            );
            Status.HasException = true;
            return Status;
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
            R3000A_RaiseInternalException(
                This, 
                EXCEPTION_OVF, 
                R3000A_CurrentPipeStage(This, EXECUTE_STAGE)
            );
            Status.HasException = true;
            return Status;
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

    default: return Status;
    }

    ASSERT(Rd);
    Status.RegIndex = RegIndex;
    Status.WritebackData = *Rd;
    return Status;
}




/* =============================================================================================
 *                                         EXECUTE STAGE
 *=============================================================================================*/

/* returns true if an exception has occured during this stage, false otherwise */
static R3000A_StageStatus R3000A_Execute(R3000A *This)
{
    int CurrentStage = R3000A_CurrentPipeStage(This, EXECUTE_STAGE);
    u32 Instruction = R3000A_InstructionAt(This, EXECUTE_STAGE);
    uint RegIndex = REG(Instruction, RT);
    u32 *Rt = &This->R[RegIndex];
    u32 Rs = This->R[REG(Instruction, RS)];

    u32 SignedImm = (i32)(i16)(Instruction & 0xFFFF);
    u32 UnsignedImm = Instruction & 0xFFFF;
    R3000A_StageStatus Status = { 0 };

    /* only care about ALU ops */
    switch (OP(Instruction)) /* group and mode of op */
    {
    case 000:
    {
        return R3000A_ExecuteSpecial(This, Instruction, *Rt, Rs);
    } break;

    /* group 1: alu immediate */
    case 010: /* addi */
    {
        u32 Result = Rs + SignedImm;
        if (OVERFLOW_I32(Result, Rs, SignedImm))
        {
            R3000A_RaiseInternalException(
                This, 
                EXCEPTION_OVF, 
                CurrentStage
            );
            Status.HasException = true;
            return Status;
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
        switch (REG(Instruction, RS))
        {
        case 0x00:
        case 0x01: /* mfc0 */
        {
            *Rt = CP0_Read(&This->CP0, REG(Instruction, RD));
        } break;
        case 0x04:
        case 0x05: /* mtc0 */
        {
            CP0_Write(&This->CP0, REG(Instruction, RD), *Rt);
            Rt = NULL;
        } break;
        case 0x10: /* rfe only */
        {
            if (0x10 == FUNCT(Instruction))
            {
                /* restore KUp, IEp and KUc, IEc */
                MASKED_LOAD(This->CP0.Status, This->CP0.Status >> 2, 0xF);
                Rt = NULL;
            }
        } break;
        default: goto DifferentInstruction;
        }
    } break;
    case 022: /* COP2 misc */
    {
        TODO("COP2 instructions");
        switch (REG(Instruction, RS))
        {
        case 0x00: 
        case 0x01: /* mfc2 */
        {
        } break;
        case 0x02:
        case 0x03: /* mtc2 */
        {
        } break;
        case 0x04:
        case 0x05: /* cfc2 */
        {
        } break;
        case 0x06:
        case 0x07: /* ctc2 */
        {
        } break;
        default: goto DifferentInstruction; 
        }
        Rt = NULL;
    } break;

DifferentInstruction:
    default: return Status;
    }

    if (Rt)
    {
        Status.RegIndex = RegIndex;
        Status.WritebackData = *Rt;
    }
    return Status;
}


/* =============================================================================================
 *                                         MEMORY STAGE
 *=============================================================================================*/

/* returns true if an exception has occured during this stage, false otherwise */
static R3000A_StageStatus R3000A_Memory(R3000A *This)
{
    u32 Instruction = R3000A_InstructionAt(This, MEMORY_STAGE);
    u32 Addr; /* calculate effective addr */
    {
        i32 Offset = (i32)(i16)(Instruction & 0xFFFF);
        u32 Base = This->R[REG(Instruction, RS)];
        Addr = Base + Offset;
    }
    uint RegIndex = REG(Instruction, RT);
    u32 Rt = This->R[RegIndex];
    u32 DataRead = 0;
    R3000A_StageStatus Status = { 0 };

    /* verify memory addr */
    if (!This->VerifyDataAddr(This->UserData, Addr))
    {
        if ((OP_GROUP(Instruction) & 0x1)) /* store instruction */
            goto StoreAddrError;
        else goto LoadAddrError;
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
        WRITE_BYTE(Addr, Rt & 0xFF);
        return Status;
    } break;
    case 051: /* sh */
    {
        if (Addr & 1)
            goto StoreAddrError;
        WRITE_HALF(Addr, Rt & 0xFFFF);
        return Status;
    } break;
    case 053: /* sw */
    {
        if (Addr & 3)
            goto StoreAddrError;
        WRITE_WORD(Addr, Rt);
        return Status;
    } break;


    case 046: /* lwr */
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
        DataRead = Rt;
        MASKED_LOAD(DataRead, Data, Mask);
    } break;
    case 042:  /* lwl */
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
        DataRead = Rt;
        MASKED_LOAD(DataRead, Data, Mask);
    } break;

    case 056: /* swr */
    {
        /* stores 'up' from a given addr to one that's divisible by 4 */
        do {
            WRITE_BYTE(Addr, Rt & 0xFF);
            Rt >>= 8;
        } while (++Addr & 0x3);
        return Status;
    } break;
    case 052: /* swl */
    {
        /* stores 'down' from a given addr + 4 to one that's divisible by 4 */
        do {
            WRITE_BYTE(Addr, Rt >> 24);
            Rt <<= 8;
        } while (Addr-- & 0x3);
        return Status;

    } break;

    default: return Status;
    }

    Status.WritebackData = DataRead;
    Status.RegIndex = RegIndex;
    return Status;

LoadAddrError:
    Status.HasException = true;
    R3000A_RaiseMemoryException(This, EXCEPTION_ADEL, Addr);
    return Status;
StoreAddrError:
    Status.HasException = true;
    R3000A_RaiseMemoryException(This, EXCEPTION_ADES, Addr);
    return Status;
}


/* =============================================================================================
 *                                         WRITEBACK STAGE
 *=============================================================================================*/

static void R3000A_Writeback(R3000A *This)
{
    (void)This;
}





static void R3000A_AdvancePipeStage(R3000A *This)
{
    This->PipeStage++;
    if (This->PipeStage >= R3000A_PIPESTAGE_COUNT)
        This->PipeStage = 0;   
}

/* returns -1 if execution should resume normally, 
 * otherwise an exception was encountered and 
 * returns the stage right before the stage that encountered the exception 
 */
static int R3000A_ExecutePipeline(R3000A *This)
{
    R3000A_StageStatus MemoryStage = R3000A_Memory(This);
    if (MemoryStage.HasException)
        return EXECUTE_STAGE;

    R3000A_StageStatus ExecuteStage = R3000A_Execute(This);
    This->R[0] = 0;
    if (MemoryStage.RegIndex)
        This->R[MemoryStage.RegIndex] = MemoryStage.WritebackData;

    if (ExecuteStage.HasException)
        return DECODE_STAGE;
    if (This->HiLoBlocking)
        return -1;

    if (ExecuteStage.RegIndex)
        This->R[ExecuteStage.RegIndex] = ExecuteStage.WritebackData;

    Bool8 HasException = R3000A_Decode(This);
    This->R[0] = 0;
    if (HasException)
        return FETCH_STAGE;

    R3000A_Writeback(This);
    This->R[0] = 0;
    return -1;
}


void R3000A_StepClock(R3000A *This)
{
    if (This->HiLoBlocking)
    {
        if (This->HiLoCyclesLeft)
        {
            /* NOTE: busy wait until hi/lo are free, 
             * because some instruction tried to access hi/lo while it's still busy */
            This->HiLoCyclesLeft--;
            return;
        }
        else 
        {
            /* free hi/lo and resume execution */
            This->HiLoBlocking = false;
        }
    }
    else
    {
        /* emulating the divider and multiplier working in the background, 
         * NOTE: hi/lo is not being accessed, so we don't busy wait unlike above */
        if (This->HiLoCyclesLeft)
            This->HiLoCyclesLeft--;

        R3000A_AdvancePipeStage(This);
        if (This->ExceptionRaised)
        {
            if (This->ExceptionCyclesLeft)
            {
                /* needs to empty all instruction in the pipeline */
                /* don't do fetching when emptying out instructions in the pipeline, 
                 * put nops in their place instead */

                int FetchStage = R3000A_CurrentPipeStage(This, FETCH_STAGE);
                This->Instruction[FetchStage] = 0;
                This->ExceptionCyclesLeft--;
            }
            else /* emptied all instructions in the pipeline */
            {
                R3000A_HandleException(This);

                /* starts fetching from the exception handler */
                R3000A_Fetch(This);
            }
        }
        else /* no exception, normal fetching */
        {
            R3000A_Fetch(This);
        }
    }


    int StageToInvalidate = R3000A_ExecutePipeline(This);
    if (-1 == StageToInvalidate)
        return;

    while (StageToInvalidate >= FETCH_STAGE)
    {
        int Stage = R3000A_CurrentPipeStage(This, StageToInvalidate);
        This->PCSave[Stage] = 0;
        This->Instruction[Stage] = 0;
        StageToInvalidate--;
    }
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
#include <inttypes.h>

#include "raylib.h"

#include "CP0.c"
#include "Disassembler.c"

#define ARGB(a_, r_, g_, b_) (Color) {.a = a_, .r = r_, .g = g_, .b = b_}

typedef enum TestSyscall 
{
    TESTSYS_WRITESTR    = 0x70000000,
    TESTSYS_WRITEHEX    = 0x70010000,
    TESTSYS_EXIT        = 0x72000000,
} TestSyscall;

typedef struct MessageQueue 
{
    char Buf[32][256];
    int Count;
} MessageQueue;

typedef struct TestOS 
{
    u8 *MemPtr;
    int MemSizeBytes;
    MessageQueue Log;
    MessageQueue Terminal;
} TestOS;


#if 1

typedef struct TextAttribute
{
    float Size, Spacing;
    Font *Fnt;
    Color Colour;
} TextAttribute;

typedef struct DisassemblyWindow 
{
    u32 BaseAddrOnScreen;
    int PCYPos;
    int x, y, Width, Height;
    int LineCount;

    char Mnemonic[2048];
    char Addresses[1024];
    char InsHexCode[1024];
    float AddressXOffset;       /* relative from x + Text.Size */
    float HexCodeXOffset;       /* relative form x + AddressXOffset */
    float MnemonicXOffset;      /* relative from x + HexCodeXOffset */

    TextAttribute Text;
    Color BgColor;
    Color PCHighlightColor;
    Color BreakpointLineColor;
} DisassemblyWindow;

typedef struct CPUStateWindow 
{
    R3000A *CPU;
    int x, y, Width, Height;
    TextAttribute Text;
    Color BgColor;
} CPUStateWindow;


typedef struct InputBuffer 
{
    Bool8 KeySpaceWasDown;
    Bool8 Run;
    float KeySpaceDownTime;
} InputBuffer;

static u32 TranslateAddr(u32 LogicalAddr)
{
    switch (LogicalAddr)
    {
    case TESTSYS_WRITESTR:
    case TESTSYS_WRITEHEX:
    case TESTSYS_EXIT:
    {
        return LogicalAddr;
    } break;
    default: 
    {
        if (IN_RANGE(0x80000000, LogicalAddr, 0xA0000000))
            return LogicalAddr - 0x80000000;
        return LogicalAddr - R3000A_RESET_VEC;
    } break;
    }
}

static Bool8 AccessOutOfRange(u32 Addr, u32 TotalSize, u32 DataSize)
{
    return (Addr > TotalSize) || (Addr + DataSize > TotalSize);
}

static void QueueMessage(MessageQueue *MsgQ, const char *Str, ...)
{
    va_list Args;
    va_start(Args, Str);
    snprintf(MsgQ->Buf[MsgQ->Count], sizeof MsgQ->Buf[0], Str, Args);
    va_end(Args);

    MsgQ->Count = (MsgQ->Count + 1) % STATIC_ARRAY_SIZE(MsgQ->Buf);
}


static u32 DbgReadFn(void *UserData, u32 Addr, R3000A_DataSize Size)
{
    TestOS *OS = UserData;
    u32 PhysAddr = TranslateAddr(Addr);
    if (!AccessOutOfRange(PhysAddr, OS->MemSizeBytes, Size))
    {
        u32 Data = 0;
        for (int i = 0; i < Size; i++)
        {
            Data |= (u32)OS->MemPtr[PhysAddr + i] << i*8;
        }
        return Data;
    }

    QueueMessage(&OS->Log, "Out of bound read at 0x%08x (size = %d).", Addr, Size);
    return 0;
}

static void DbgWriteFn(void *UserData, u32 Addr, u32 Data, R3000A_DataSize Size)
{
    TestOS *OS = UserData;
    u32 PhysAddr = TranslateAddr(Addr);
    if (!AccessOutOfRange(PhysAddr, OS->MemSizeBytes, Size))
    {
        for (int i = 0; i < Size; i++)
        {
            OS->MemPtr[PhysAddr + i] = Data;
            Data >>= 8;
        }
        return;
    }
    if (TESTSYS_EXIT == Addr)
    {
        QueueMessage(&OS->Log, "Program exited.\n");
        return;
    }
    if (TESTSYS_WRITEHEX == Addr)
    {
        QueueMessage(&OS->Terminal, "0x%08x", Data);
        return;
    }
    if (TESTSYS_WRITESTR == Addr)
    {
        Addr = Data;
        PhysAddr = TranslateAddr(Data);
        if (!AccessOutOfRange(PhysAddr, OS->MemSizeBytes, Size))
        {
            int StrLen = 0;
            for (int i = PhysAddr; i < OS->MemSizeBytes && OS->MemPtr[i]; i++)
            {
                StrLen++;
            }
            const char *String = (const char *)&OS->MemPtr[PhysAddr];
            QueueMessage(&OS->Terminal, "%.*s", StrLen, String);
            return;
        }
    }
    QueueMessage(&OS->Log, "Out of bound write to 0x%08x (0x%80x, size = %d)\n", Addr, Data, Size);
}

static Bool8 DbgVerifyAddrFn(void *UserData, u32 Addr)
{
    (void)UserData, (void)Addr;
    return true;
}


static void HandleInput(InputBuffer *Input, R3000A *Mips)
{
    Bool8 ShouldStepClock = 
        Input->Run || ((IsKeyUp(KEY_SPACE) && Input->KeySpaceWasDown == true) || Input->KeySpaceDownTime > .5);

    Input->KeySpaceWasDown = IsKeyDown(KEY_SPACE);
    if (Input->KeySpaceWasDown)
    {
        Input->KeySpaceDownTime += GetFrameTime();
    }
    else
    {
        Input->KeySpaceDownTime = 0;
    }
    if (GetKeyPressed() == KEY_ENTER)
    {
        Input->Run = !Input->Run;
    }


    if (ShouldStepClock)
        R3000A_StepClock(Mips);
}

static void MemCpy(void *Dst, const void *Src, size_t SizeBytes)
{
    u8 *DstPtr = Dst;
    const u8 *SrcPtr = Src;
    while (SizeBytes-- > 0)
        *DstPtr++ = *SrcPtr++;
}

static void UpdateDisassemblyStrings(DisassemblyWindow *DisasmWindow, u32 PC, const u8 *Mem, int MemSizeBytes)
{
    u32 PhysBaseAddr = TranslateAddr(DisasmWindow->BaseAddrOnScreen);

    int DisasmSizeLeft = sizeof DisasmWindow->Mnemonic, 
        AddrSizeLeft = sizeof DisasmWindow->Addresses,
        InsHexCodeSizeLeft = sizeof DisasmWindow->InsHexCode;
    char *DisasmBuffer = DisasmWindow->Mnemonic,
         *AddrBuffer = DisasmWindow->Addresses,
         *InsHexCodeBuffer = DisasmWindow->InsHexCode;
    for (int i = 0; i < DisasmWindow->LineCount; i++)
    {
        u32 Instruction = 0;
        int MnemonicLen = 0;
        int AddrLen = 0;
        int InsHexCodeLen = 0;

        u32 CurrentPhysAddr = PhysBaseAddr + i*sizeof(Instruction);
        u32 CurrentVirtAddr = DisasmWindow->BaseAddrOnScreen + i*sizeof(Instruction);
        if (!AccessOutOfRange(CurrentPhysAddr, MemSizeBytes, sizeof(Instruction)))
        {
            MemCpy(&Instruction, Mem + CurrentPhysAddr, sizeof(Instruction));

            /* update disassembly window */
            if (DisasmSizeLeft > 0)
            {
                MnemonicLen = R3000A_Disasm(
                    Instruction, 
                    CurrentVirtAddr,
                    0, 
                    DisasmBuffer, 
                    DisasmSizeLeft
                );

                if (DisasmSizeLeft > 1)
                {
                    DisasmBuffer[MnemonicLen + 0] = '\n';
                    DisasmBuffer[MnemonicLen + 1] = '\0';
                    MnemonicLen += 1;
                }
            }
        }
        else
        {
            if (DisasmSizeLeft > 0)
            {
                MnemonicLen = sizeof "???\n" - 1;
                MemCpy(DisasmBuffer, "???\n", MnemonicLen + 1);
            }
        }

        if (AddrSizeLeft > 0)
        {
            AddrLen = snprintf(
                AddrBuffer, AddrSizeLeft, 
                "%08x:\n", 
                PC + i*(int)sizeof(Instruction)
            );
        }
        if (InsHexCodeSizeLeft > 0)
        {
            InsHexCodeLen = snprintf(
                InsHexCodeBuffer, InsHexCodeSizeLeft, 
                "%08x\n", Instruction
            );
        }

        DisasmBuffer += MnemonicLen;
        DisasmSizeLeft -= MnemonicLen;
        InsHexCodeBuffer += InsHexCodeLen;
        InsHexCodeSizeLeft -= InsHexCodeLen;
        AddrBuffer += AddrLen;
        AddrSizeLeft -= AddrLen;
    }
}

static void UpdateDisassemblyWindow(
    DisassemblyWindow *DisasmWindow, 
    u32 PC, 
    const u8 *Mem, 
    int MemSizeBytes, 
    Bool8 AlwaysUpdateString
)
{
    u32 BaseAddr = DisasmWindow->BaseAddrOnScreen;
    /* PC is out of screen */
    if (!IN_RANGE(BaseAddr, PC, BaseAddr + (DisasmWindow->LineCount - 1)*sizeof(u32)))
    {
        /* this current PC will be the base addr */
        DisasmWindow->BaseAddrOnScreen = PC;
        BaseAddr = PC;
        UpdateDisassemblyStrings(DisasmWindow, PC, Mem, MemSizeBytes);
    }
    else if (AlwaysUpdateString)
    {
        UpdateDisassemblyStrings(DisasmWindow, PC, Mem, MemSizeBytes);
    }

    DisasmWindow->PCYPos = (PC - BaseAddr) / sizeof(u32) * (int)DisasmWindow->Text.Size;
}

static void ResizeDisasmWindow(DisassemblyWindow *DisasmWindow, int x, int y, int w, int h)
{
    DisasmWindow->x = x;
    DisasmWindow->y = y;
    DisasmWindow->Width = w;
    DisasmWindow->Height = h;
    DisasmWindow->LineCount = h / DisasmWindow->Text.Size;
}

static void ResizeCPUStateWindow(CPUStateWindow *Window, int x, int y, int w, int h)
{
    Window->x = x;
    Window->y = y;
    Window->Width = w;
    Window->Height = h;
}


static void DrawStr(TextAttribute TextAttr, int x, int y, const char *Str)
{
    Vector2 Pos = {.x = x, .y = y};
    DrawTextEx(*TextAttr.Fnt, Str, Pos, TextAttr.Size, TextAttr.Spacing, TextAttr.Colour);
}

typedef enum StrBoxAlignType 
{
    STRBOX_ALIGN_CENTER = 0, 
    STRBOX_ALIGN_LEFT, 
    STRBOX_ALIGN_RIGHT,
} StrBoxAlignType;
static void DrawStrBox(
    int x, int y, int w, int h, 
    Color BoxColor, 
    TextAttribute TextAttr, 
    const char *Str, 
    StrBoxAlignType StrAlign)
{
    int Align = 0;
    switch (StrAlign)
    {
    case STRBOX_ALIGN_LEFT: 
    {
        Align = 0;
    } break;
    case STRBOX_ALIGN_RIGHT:
    {
        int TextLenPixel = MeasureText(Str, TextAttr.Size);
        Align = w - TextLenPixel;
    } break;
    case STRBOX_ALIGN_CENTER:
    {
        int TextLenPixel = MeasureText(Str, TextAttr.Size);
        Align = (w - TextLenPixel)/2;
    } break;
    }
    DrawRectangle(x, y, w, h, BoxColor);
    DrawStr(TextAttr, x + Align, y, Str);
}

static void DrawDisassemblyWindow(const DisassemblyWindow *DisasmWindow, Bool8 HighlightPC)
{
    ASSERT(NULL != DisasmWindow->Text.Fnt);
    SetTextLineSpacing(DisasmWindow->Text.Size);

    /* draw the window itself */
    DrawRectangle(
        DisasmWindow->x, DisasmWindow->y, DisasmWindow->Width, DisasmWindow->Height, DisasmWindow->BgColor
    );

    /* draw breakpoint line */
    DrawRectangle(
        DisasmWindow->x, DisasmWindow->y, DisasmWindow->Text.Size, DisasmWindow->Height, DisasmWindow->BreakpointLineColor
    );


    /* highlight the line that the pc points to */
    if (HighlightPC && IN_RANGE(0, DisasmWindow->PCYPos, DisasmWindow->Height - DisasmWindow->Text.Size))
    {
        DrawRectangle(
            DisasmWindow->x, DisasmWindow->PCYPos, DisasmWindow->Width, DisasmWindow->Text.Size, DisasmWindow->PCHighlightColor
        );
    }


    int x = DisasmWindow->x + DisasmWindow->Text.Size;
    int y = DisasmWindow->y;
    /* draw addresses */
    x += DisasmWindow->AddressXOffset;
    DrawStr(DisasmWindow->Text, x, y, DisasmWindow->Addresses);

    /* draw hex codes */
    x += DisasmWindow->HexCodeXOffset;
    DrawStr(DisasmWindow->Text, x, y, DisasmWindow->InsHexCode);

    /* draw instruction mnemonics */
    x += DisasmWindow->MnemonicXOffset;
    DrawStr(DisasmWindow->Text, x, y, DisasmWindow->Mnemonic);
}


static void DrawCPUState(const CPUStateWindow *Window)
{
    const R3000A *CPU = Window->CPU;
    ASSERT(NULL != Window->Text.Fnt);
    ASSERT(NULL != CPU);

    SetTextLineSpacing(Window->Text.Size);

    /* draw registers */
    Bool8 DisplayHex = true;
    uint LeftSpace = 0.1 * Window->Width;
    uint RightSpace = 0.1 * Window->Width;
    uint RegisterPerLine = 4;
    uint SpaceBetweenRegisterBox = 10;
    uint RegisterBoxWidth = (-LeftSpace + Window->Width - RightSpace) / RegisterPerLine - SpaceBetweenRegisterBox;
    uint RegisterBoxHeight = 2*(Window->Text.Size + 0.1 * Window->Text.Size);

    uint BaseX = Window->x;
    uint BaseY = Window->y;
    uint OffsetX = LeftSpace;
    uint OffsetY = 0;
    uint x = BaseX;
    uint y = BaseY;
    for (uint i = 0; i < STATIC_ARRAY_SIZE(CPU->R); i++)
    {
        x = OffsetX + BaseX;
        y = OffsetY + BaseY;
        if ((i + 1) % RegisterPerLine == 0)
        {
            OffsetX = LeftSpace;
            OffsetY += RegisterBoxHeight + SpaceBetweenRegisterBox;
        }
        else
        {
            OffsetX += RegisterBoxWidth + SpaceBetweenRegisterBox;
        }

        /* draw register name */
        DrawStrBox(
            x, y, RegisterBoxWidth, RegisterBoxHeight/2, 
            ARGB(0x80, 0, 0, 0), 
            Window->Text, 
            TextFormat("R%d", i),
            STRBOX_ALIGN_CENTER
        );

        /* draw register content */
        const char *FmtStr = DisplayHex? 
            "%08"PRIx32 
            : "%"PRIi32;
        DrawStrBox(
            x, y + RegisterBoxHeight/2, RegisterBoxWidth, RegisterBoxHeight/2,
            ARGB(0xC0, 0, 0, 0), 
            Window->Text,
            TextFormat(FmtStr, CPU->R[i]),
            STRBOX_ALIGN_CENTER
        );
    }

    x = OffsetX + BaseX;
    y = OffsetY + BaseY;
}


int main(void)
{
    int Width = 1080;
    int Height = 720;
    Color BgColor = ARGB(0, 0x80, 0x80, 0x80);
    TestOS OS = {
        0
    };
    R3000A Mips = R3000A_Init(&OS, DbgReadFn, DbgWriteFn, DbgVerifyAddrFn, DbgVerifyAddrFn);


    InitWindow(Width, Height, "R3000");
    SetWindowState(FLAG_WINDOW_RESIZABLE);
    SetTargetFPS(60);

    Font DefaultFont = LoadFont("resources/CascadiaMono.ttf");
    DisassemblyWindow DisasmWindow = {
        .Text = {
            .Fnt = &DefaultFont,
            .Spacing = 1,
            .Size = 16, 
            .Colour = RAYWHITE,
        },

        .BgColor = ARGB(0xFF, 0x50, 0x50, 0x50),
        .PCHighlightColor = ARGB(0x80, 0xA0, 0xA0, 0),
        .BreakpointLineColor = GRAY,

        .LineCount = Height / 16, 
        .AddressXOffset = 10,
        .HexCodeXOffset = 100,
        .MnemonicXOffset = 100,

        .x = 0, 
        .y = 0, 
        .Width = Width/2,
        .Height = Height,
    };

    CPUStateWindow CPUState = {
        .Text = {
            .Fnt = &DefaultFont,
            .Spacing = 1,
            .Size = 16,
            .Colour = WHITE,
        },
        .CPU = &Mips,

        .x = DisasmWindow.Width,
        .y = 0,
        .Width = Width - DisasmWindow.Width,
        .Height = Height,
    };

    InputBuffer Input = { 0 };

    Bool8 ForceUpdateDisassembly = false;
    while (!WindowShouldClose())
    {
        if (IsFileDropped())
        {
            if (OS.MemPtr)
                UnloadFileData(OS.MemPtr);

            FilePathList List = LoadDroppedFiles();
            OS.MemPtr = LoadFileData(List.paths[0], &OS.MemSizeBytes);
            UnloadDroppedFiles(List);
            ForceUpdateDisassembly = true;
        }
        if (IsWindowResized())
        {
            Width = GetScreenWidth();
            Height = GetScreenHeight();
            ResizeDisasmWindow(&DisasmWindow, 0, 0, Width/2, Height);
            ResizeCPUStateWindow(&CPUState, Width/2, 0, Width/2, Height);
            ForceUpdateDisassembly = true;
        }
        HandleInput(&Input, &Mips);
        UpdateDisassemblyWindow(&DisasmWindow, Mips.PC, OS.MemPtr, OS.MemSizeBytes, ForceUpdateDisassembly);
        ForceUpdateDisassembly = false;

        BeginDrawing();
            ClearBackground(BgColor);
            DrawDisassemblyWindow(&DisasmWindow, true);
            DrawCPUState(&CPUState);
        EndDrawing();
    }

    if (OS.MemPtr)
        UnloadFileData(OS.MemPtr);
    UnloadFont(DefaultFont);
    CloseWindow();
    return 0;
}

#else

typedef enum ScreenFlag 
{
    SCREEN_CLEAR_ON_Y_WRAP = 1,
} ScreenFlag;
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
    ScreenFlag Flags;
} ScreenBuffer;

typedef struct Buffer 
{
    u8 *Ptr;
    iSize Size;
} Buffer;



#define STREQU(Buf, ConstStr) (0 == strncmp(Buf, ConstStr, sizeof ConstStr - 1))
#define STATUS_HEIGHT (21)
#define STATUS_WIDTH (14*4)
#define LOG_HEIGHT (4)
#define LOG_WIDTH STATUS_WIDTH
#define SEPARATOR_WIDTH 1
#define SEPARATOR_HEIGHT 1
#define TERM_HEIGHT (LOG_HEIGHT + STATUS_HEIGHT)
#define TERM_WIDTH  80
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
    .BufferSizeBytes = sizeof sMasterScreenBuffer,
    .Flags = SCREEN_CLEAR_ON_Y_WRAP,
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


static void ScreenWriteStr(ScreenBuffer *Screen, const char *Str, iSize MaxLen, uint Flags)
{
    if ((Screen->Flags & SCREEN_CLEAR_ON_Y_WRAP)
    && Screen->WriteCursor.x == 0 && Screen->WriteCursor.y == 0)
    {
        ScreenClear(Screen, ' ');
    }

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
                return;
            if (!(Flags & WRAP_Y) && (Wrapped & WRAP_Y))
                return;
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
    case TESTSYS_WRITEHEX:
    case TESTSYS_EXIT:
    case TESTSYS_CLRSCR:
    {
        return LogicalAddr;
    } break;
    default: 
    {
        if (IN_RANGE(0x80000000, LogicalAddr, 0xA0000000))
            return LogicalAddr - 0x80000000;
        return LogicalAddr - R3000A_RESET_VEC;
    } break;
    }
}

static void MipsWrite(void *UserData, u32 Addr, u32 Data, R3000A_DataSize Size)
{
    Buffer *Buf = UserData;
    Addr = TranslateAddr(Addr);
    if (Addr == (u32)TESTSYS_WRITESTR)
    {
        Data -= R3000A_RESET_VEC;
        if (Data >= Buf->Size)
        {
            SCREEN_PRINTF(&sLogWindow, WRAP_X, 
                "Invalid string address: 0x%08x\n", 
                Data + R3000A_RESET_VEC
            );
        }
        else
        {
            iSize MaxLength = Buf->Size - (&Buf->Ptr[Data] - Buf->Ptr);
            ScreenWriteStr(&sTerminalWindow, (const char *)&Buf->Ptr[Data], MaxLength, false);
        }
    }
    else if (Addr == (u32)TESTSYS_WRITEHEX)
    {
        SCREEN_PRINTF(&sTerminalWindow, 0, "0x%x", Data);
    }
    else if (Addr == (u32)TESTSYS_CLRSCR)
    {
        ScreenClear(&sTerminalWindow, ' ');
    }
    else if (Addr == (u32)TESTSYS_EXIT)
    {
        sShouldContinue = false;
    }
    else if (Addr + Size <= Buf->Size && Addr < Buf->Size)
    {
        SCREEN_PRINTF(&sLogWindow, WRAP_X, 
            "Writing 0x%0*x to 0x%08x\n", 
            Size, Data, Addr + R3000A_RESET_VEC
        );
        for (int i = 0; i < (int)Size; i++)
        {
            Buf->Ptr[Addr + i] = Data & 0xFF;
            Data >>= 8;
        }
    }
    else
    {
        SCREEN_PRINTF(&sLogWindow, WRAP_X, 
            "Out of bound write to 0x%08x with %x (size %d)\n", 
            Addr + R3000A_RESET_VEC, Data, Size
        );
    }
}

static u32 MipsRead(void *UserData, u32 Addr, R3000A_DataSize Size)
{
    Buffer *Buf = UserData;
    Addr = TranslateAddr(Addr);
    if (Addr + Size <= Buf->Size && Addr < Buf->Size)
    {
        u32 Data = 0;
        for (int i = 0; i < (int)Size; i++)
        {
            Data |= (u32)Buf->Ptr[Addr + i] << 8*i;
        }
        return Data;
    }
    else
    {
        SCREEN_PRINTF(&sLogWindow, WRAP_X,
            "Out of bound read at 0x%08x, size = %d\n", 
            Addr + R3000A_RESET_VEC, Size
        );
    }
    return 0;
}

static Bool8 MipsVerify(void *UserData, u32 Addr)
{
    return true;
}


static u32 ParseAddr(const char *Buf)
{
    u32 Addr = 0;
    const char *Ptr = Buf;

    /* skip space */
    while (' ' == *Ptr && *Ptr)
        Ptr++;
    if ('\0' == *Ptr)
        return Addr;

    /* addr is gonna be in hex anyway, so we ignore 0x */
    if (STREQU(Ptr, "0x"))
        Ptr += 2;
    while ('\0' != *Ptr)
    {
        char Ch = *Ptr++;
        char UpperCh = Ch & ~(1 << 5);
        if (IN_RANGE('0', Ch, '9'))
        {
            Addr *= 16;
            Addr += Ch - '0';
        }
        else if (IN_RANGE('A', UpperCh, 'F'))
        {
            Addr *= 16;
            Addr += UpperCh - 'A' + 10;
        }
        else if ('_' == Ch)
        {
            /* do nothing */
        }
        else break;
    }
    return Addr;
}


static Bool8 ProcessCLI(R3000A *Mips)
{
    static Bool8 ShouldHalt = true;
    static u32 HaltAddr = 1;

    if (Mips->PC == HaltAddr) 
        ShouldHalt = true;
    if (!ShouldHalt)
        return true;

    char InputBuffer[64];
    if (NULL == fgets(InputBuffer, sizeof InputBuffer, stdin))
        return false;

    if (STREQU(InputBuffer, "setbp"))
    {
        HaltAddr = ParseAddr(InputBuffer + sizeof("setbp") - 1);
    }
    else if (STREQU(InputBuffer, "cont"))
    {
        ShouldHalt = false;
    }
    return !STREQU(InputBuffer, "quit");
}

static void UpdateDisassembly(ScreenBuffer *Screen, const R3000A *Mips, int StartX, int StartY)
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
            if (PhysAddr + 4 <= Buf->Size && PhysAddr < Buf->Size)
            {
                u32 Instruction;
                memcpy(&Instruction, Buf->Ptr + PhysAddr, sizeof Instruction);
                R3000A_Disasm(
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

static void DumpState(const R3000A *Mips)
{
    static R3000A LastState;
    int RegisterCount = STATIC_ARRAY_SIZE(Mips->R);
    int RegisterBoxPerLine = 4;
    int y = 0;

    ScreenClear(&sStatusWindow, ' ');
    if (sLogWindow.WriteCursor.x == 0 && sLogWindow.WriteCursor.y == 0)
    {
        ScreenClear(&sLogWindow, ' ');
    }
    /* dump regs */
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
    R3000A Mips = R3000A_Init(
        &Buf, 
        MipsRead, MipsWrite,
        MipsVerify, MipsVerify
    );

    sShouldContinue = true;
    do {
        R3000A_StepClock(&Mips);
        DumpState(&Mips);
        if (!CLIDisable)
        {
            sShouldContinue = sShouldContinue && ProcessCLI(&Mips);
        }
    } while (sShouldContinue);

    free(Buf.Ptr);
    return 0;
}
#endif /* GUI */
#endif /* STANDALONE */
#endif /* R3000A_C */
