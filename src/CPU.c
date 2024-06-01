#include <string.h> /* memcpy */

#include "Common.h"
#include "Ps1.h"
#include "CPU.h"

#define SLOT_BRANCH 0x2
#define SLOT_DELAY  0x1
#define SLOT_NONE   0x0

static u32 CPU_ReadReg(CPU *Cpu, uint RegIndex);
static void CPU_WriteReg(CPU *Cpu, uint RegIndex, u32 Data);
static void CPU_SetDelayedLoad(CPU *Cpu, uint Index, u32 Data);

static void CPU_Lui(CPU *Cpu, u32 Instruction);
static void CPU_Ori(CPU *Cpu, u32 Instruction);
static void CPU_Sw(CPU *Cpu, u32 Instruction);
static void CPU_Sll(CPU *Cpu, u32 Instruction);
static void CPU_Addiu(CPU *Cpu, u32 Instruction);
static void CPU_J(CPU *Cpu, u32 Instruction);
static void CPU_Or(CPU *Cpu, u32 Instruction);
static void CPU_Bne(CPU *Cpu, u32 Instruction);
static void CPU_Addi(CPU *Cpu, u32 Instruction);
static void CPU_Lw(CPU *Cpu, u32 Instruction);
static void CPU_Sltu(CPU *Cpu, u32 Instruction);
static void CPU_Addu(CPU *Cpu, u32 Instruction);
static void CPU_Sh(CPU *Cpu, u32 Instruction);
static void CPU_Jal(CPU *Cpu, u32 Instruction);
static void CPU_Andi(CPU *Cpu, u32 Instruction);
static void CPU_Sb(CPU *Cpu, u32 Instruction);
static void CPU_Jr(CPU *Cpu, u32 Instruction);
static void CPU_Lb(CPU *Cpu, u32 Instruction);
static void CPU_Beq(CPU *Cpu, u32 Instruction);
static void CPU_And(CPU *Cpu, u32 Instruction);
static void CPU_Add(CPU *Cpu, u32 Instruction);
static void CPU_Bgtz(CPU *Cpu, u32 Instruction);
static void CPU_Blez(CPU *Cpu, u32 Instruction);
static void CPU_Lbu(CPU *Cpu, u32 Instruction);
static void CPU_Jalr(CPU *Cpu, u32 Instruction);
static void CPU_Bcc(CPU *Cpu, u32 Instruction);
static void CPU_Slti(CPU *Cpu, u32 Instruction);
static void CPU_Subu(CPU *Cpu, u32 Instruction);
static void CPU_Sra(CPU *Cpu, u32 Instruction);
static void CPU_Div(CPU *Cpu, u32 Instruction);
static void CPU_Mf(CPU *Cpu, u32 Instruction, u32 HiLoData);
static void CPU_Srl(CPU *Cpu, u32 Instruction);
static void CPU_Sltiu(CPU *Cpu, u32 Instruction);
static void CPU_Divu(CPU *Cpu, u32 Instruction);
static void CPU_Slt(CPU *Cpu, u32 Instruction);
static u32 CPU_Mt(CPU *Cpu, u32 Instruction);
static void CPU_Lhu(CPU *Cpu, u32 Instruction);
static void CPU_Sllv(CPU *Cpu, u32 Instruction);
static void CPU_Lh(CPU *Cpu, u32 Instruction);
static void CPU_Nor(CPU *Cpu, u32 Instruction);
static void CPU_Srav(CPU *Cpu, u32 Instruction);
static void CPU_Srlv(CPU *Cpu, u32 Instruction);
static void CPU_Multu(CPU *Cpu, u32 Instruction);
static void CPU_Xor(CPU *Cpu, u32 Instruction);
static void CPU_Mult(CPU *Cpu, u32 Instruction);
static void CPU_Sub(CPU *Cpu, u32 Instruction);
static void CPU_Xori(CPU *Cpu, u32 Instruction);
static void CPU_Lwl(CPU *Cpu, u32 Instruction);
static void CPU_Lwr(CPU *Cpu, u32 Instruction);
static void CPU_Swl(CPU *Cpu, u32 Instruction);
static void CPU_Swr(CPU *Cpu, u32 Instruction);
static void CPU_LwC2(CPU *Cpu, u32 Instruction);
static void CPU_SwC2(CPU *Cpu, u32 Instruction);

static void CPU_Branch(CPU *Cpu, u32 Instruction);
static Bool8 OverflowOnAdd(u32 A, u32 B);
static Bool8 OverflowOnSub(u32 A, u32 B);

static void CPU_Cop0_Mtc(CPU *Cpu, u32 Instruction);
static void CPU_Cop0_Mfc(CPU *Cpu, u32 Instruction);
static void CPU_Cop0_Rfe(CPU *Cpu, u32 Instruction);



void CPU_Reset(CPU *Cpu, PS1 *Ps1)
{
    u32 ResetVector = 0xBFC00000;
    *Cpu = (CPU) {
        .Bus = Ps1,
        .CurrentInstructionPC = ResetVector,
        .NextInstructionPC = ResetVector, 
        .PC = ResetVector + sizeof(u32),
    };
}

void CPU_Clock(CPU *Cpu)
{
    if (Cpu->HiLoBlocking)
    {
        if (Cpu->HiLoCyclesLeft)
        {
            Cpu->HiLoCyclesLeft--;
            return;
        }

        Cpu->HiLoBlocking = false;
        CPU_DecodeExecute(Cpu, Cpu->CurrentInstruction);
        return;
    }

    /* simulate divider working in the background */
    if (Cpu->HiLoCyclesLeft > 0)
    {
        Cpu->HiLoCyclesLeft--;
    }

    /* check pc */
    Cpu->CurrentInstructionPC = Cpu->NextInstructionPC;
    if (Cpu->CurrentInstructionPC & 3)
    {
        CPU_GenerateException(Cpu, CPU_EXCEPTION_LOAD_ADDR_ERR);
        return;
    }

    /* read next instruction and update pc */
    Cpu->CurrentInstruction = PS1_Read32(Cpu->Bus, Cpu->CurrentInstructionPC);
    Cpu->NextInstructionPC = Cpu->PC;
    Cpu->PC += 4;

    /*  load the data delayed and then reset it */
    CPU_WriteReg(Cpu, Cpu->LoadIndex, Cpu->LoadValue);
    CPU_SetDelayedLoad(Cpu, 0, 0);

    CPU_DecodeExecute(Cpu, Cpu->CurrentInstruction);
    Cpu->Slot >>= 1;

    /*  update input regs  */
    memcpy(Cpu->R, Cpu->OutR, sizeof Cpu->R);
}

void CPU_DecodeExecute(CPU *Cpu, u32 Instruction)
{
    switch (OP(Instruction))
    {
    case 0x00: /* special */
    {
        switch (FUNCT(Instruction))
        {
        case 0x00: CPU_Sll(Cpu, Instruction); break;
        case 0x25: CPU_Or(Cpu, Instruction); break;
        case 0x2B: CPU_Sltu(Cpu, Instruction); break;
        case 0x21: CPU_Addu(Cpu, Instruction); break;
        case 0x24: CPU_And(Cpu, Instruction); break;
        case 0x20: CPU_Add(Cpu, Instruction); break;
        case 0x23: CPU_Subu(Cpu, Instruction); break;
        case 0x03: CPU_Sra(Cpu, Instruction); break;
        case 0x1A: CPU_Div(Cpu, Instruction); break;
        case 0x12: CPU_Mf(Cpu, Instruction, Cpu->Lo); break;
        case 0x02: CPU_Srl(Cpu, Instruction); break;
        case 0x1B: CPU_Divu(Cpu, Instruction); break;
        case 0x10: CPU_Mf(Cpu, Instruction, Cpu->Hi); break;
        case 0x2A: CPU_Slt(Cpu, Instruction); break;
        case 0x13: Cpu->Lo = CPU_Mt(Cpu, Instruction); break;
        case 0x11: Cpu->Hi = CPU_Mt(Cpu, Instruction); break;
        case 0x04: CPU_Sllv(Cpu, Instruction); break;
        case 0x27: CPU_Nor(Cpu, Instruction); break;
        case 0x07: CPU_Srav(Cpu, Instruction); break;
        case 0x06: CPU_Srlv(Cpu, Instruction); break;
        case 0x19: CPU_Multu(Cpu, Instruction); break;
        case 0x26: CPU_Xor(Cpu, Instruction); break;
        case 0x18: CPU_Mult(Cpu, Instruction); break;
        case 0x22: CPU_Sub(Cpu, Instruction); break;

        case 0x0C: CPU_GenerateException(Cpu, CPU_EXCEPTION_SYSCALL); break;
        case 0x0D: CPU_GenerateException(Cpu, CPU_EXCEPTION_BREAK); break;

        case 0x08: CPU_Jr(Cpu, Instruction); break;
        case 0x09: CPU_Jalr(Cpu, Instruction); break;
        default: 
        {
            goto IllegalInstruction;
        } break;
        }
    } break;
    case 0x10: /* cop0 */
    {
        switch (REG(Instruction, RS)) /* rs contains op */
        {
        case 0x00: CPU_Cop0_Mfc(Cpu, Instruction); break;
        case 0x04: CPU_Cop0_Mtc(Cpu, Instruction); break;
        case 0x10: /* COP0 op */
        {
            if ((Instruction & 0x3F) == 0x10)
            {
                CPU_Cop0_Rfe(Cpu, Instruction);
            }
            else 
            {
                goto IllegalInstruction;
            }
        } break;
        default: 
        {
            goto IllegalInstruction;
        } break;
        }
    } break;
    case 0x11: /* cop1 */
    {
        goto UnusableCoprocessor;
    } break;
    case 0x12: /* cop2 (GTE) */
    {
        TODO("cop2 (gte)");
    } break;
    case 0x13: /* cop3 */
    {
        goto UnusableCoprocessor;
    } break;

    case 0x09: CPU_Addiu(Cpu, Instruction); break;
    case 0x0D: CPU_Ori(Cpu, Instruction); break;
    case 0x0F: CPU_Lui(Cpu, Instruction); break;
    case 0x08: CPU_Addi(Cpu, Instruction); break;
    case 0x0C: CPU_Andi(Cpu, Instruction); break;
    case 0x0A: CPU_Slti(Cpu, Instruction); break;
    case 0x0B: CPU_Sltiu(Cpu, Instruction); break;
    case 0x0E: CPU_Xori(Cpu, Instruction); break;

    case 0x2B: CPU_Sw(Cpu, Instruction); break;
    case 0x29: CPU_Sh(Cpu, Instruction); break;
    case 0x28: CPU_Sb(Cpu, Instruction); break;
    case 0x2A: CPU_Swl(Cpu, Instruction); break;
    case 0x2E: CPU_Swr(Cpu, Instruction); break;
    case 0x23: CPU_Lw(Cpu, Instruction); break;
    case 0x25: CPU_Lhu(Cpu, Instruction); break;
    case 0x21: CPU_Lh(Cpu, Instruction); break;
    case 0x20: CPU_Lb(Cpu, Instruction); break;
    case 0x24: CPU_Lbu(Cpu, Instruction); break;
    case 0x22: CPU_Lwl(Cpu, Instruction); break;
    case 0x26: CPU_Lwr(Cpu, Instruction); break;

    case 0x30 + 2: CPU_LwC2(Cpu, Instruction); break;
    case 0x38 + 2: CPU_SwC2(Cpu, Instruction); break;

    case 0x01: CPU_Bcc(Cpu, Instruction); break;
    case 0x06: CPU_Blez(Cpu, Instruction); break;
    case 0x07: CPU_Bgtz(Cpu, Instruction); break;
    case 0x05: CPU_Bne(Cpu, Instruction); break;
    case 0x04: CPU_Beq(Cpu, Instruction); break;
    case 0x02: CPU_J(Cpu, Instruction); break;
    case 0x03: CPU_Jal(Cpu, Instruction); break;

    case 0x30 + 0: /* lwc0 */
    case 0x30 + 1: /* lwc1 */
    case 0x30 + 3: /* lwc3 */
    case 0x38 + 0: /* swc0 */
    case 0x38 + 1: /* swc1 */
    case 0x38 + 3: /* swc3 */
    {
UnusableCoprocessor:
        LOG("Unusable coprocessor %d: %08x at PC=%08x\n", OP(Instruction) & 3, Instruction, Cpu->CurrentInstructionPC);
        CPU_GenerateException(Cpu, CPU_EXCEPTION_COP_ERR);
    } break;
IllegalInstruction:
    default:
    {
        LOG("Illegal instruction %08x at PC=%08x\n", Instruction, Cpu->CurrentInstructionPC);
        CPU_GenerateException(Cpu, CPU_EXCEPTION_ILLEGAL_INS);
    } break;
    }
}

void CPU_GenerateException(CPU *Cpu, CPU_Exception Exception)
{
    u32 ExceptionHandlerAddr = 0x80000080;
    if (Cpu->SR & (1 << 22)) /* BEV set (Boot Exception Vector) */
    {
        ExceptionHandlerAddr = 0xBFC00180;
    }

    /* push new interrupt enable-user mode flags (0b00) */
    u32 ExceptionModeStack = Cpu->SR & 0x3F;
    ExceptionModeStack <<= 2;
    Cpu->SR = (Cpu->SR & ~0x3F) | (ExceptionModeStack & 0x3F);

    /* update exception code */
    Cpu->Cause = Exception << 2;

    /* set EPC and immediately jump to exception handler */
    Cpu->EPC = Cpu->CurrentInstructionPC;
    Cpu->NextInstructionPC = ExceptionHandlerAddr;
    Cpu->PC = ExceptionHandlerAddr + 4;

    if (Cpu->Slot == SLOT_DELAY)
    {
        Cpu->Cause |= 1lu << 31;
        Cpu->EPC -= 4;
    }
}



static u32 CPU_ReadReg(CPU *Cpu, uint RegIndex)
{
    return Cpu->R[RegIndex];
}

static void CPU_WriteReg(CPU *Cpu, uint RegIndex, u32 Data)
{
    Cpu->OutR[RegIndex] = Data;
    Cpu->OutR[0] = 0; /*  R0 is always 0  */
}

static void CPU_SetDelayedLoad(CPU *Cpu, uint Index, u32 Data)
{
    Cpu->LoadIndex = Index;
    Cpu->LoadValue = Data;
}


static void CPU_Lui(CPU *Cpu, u32 Instruction)
{
    u32 Data = U16(Instruction) << 16;
    CPU_WriteReg(Cpu, REG(Instruction, RT), Data);
}

static void CPU_Ori(CPU *Cpu, u32 Instruction)
{
    u32 Rs = CPU_ReadReg(Cpu, REG(Instruction, RS));
    u32 Imm = U16(Instruction);
    CPU_WriteReg(Cpu, REG(Instruction, RT), Rs | Imm);
}

static void CPU_Sw(CPU *Cpu, u32 Instruction)
{
    u32 Rt = CPU_ReadReg(Cpu, REG(Instruction, RT));
    u32 Addr = CPU_ReadReg(Cpu, REG(Instruction, RS)) + I16(Instruction);
    if (Addr & 3)
    {
        CPU_GenerateException(Cpu, CPU_EXCEPTION_STORE_ADDR_ERR);
    }
    else
    {
        PS1_Write32(Cpu->Bus, Addr, Rt);
    }
}

static void CPU_Sll(CPU *Cpu, u32 Instruction)
{
    u32 Rt = CPU_ReadReg(Cpu, REG(Instruction, RT));
    u32 ShiftAmount = SHAMT(Instruction);
    CPU_WriteReg(Cpu, REG(Instruction, RD), Rt << ShiftAmount);
}

static void CPU_Addiu(CPU *Cpu, u32 Instruction)
{
    u32 Rs = CPU_ReadReg(Cpu, REG(Instruction, RS));
    u32 Imm = I16(Instruction);
    CPU_WriteReg(Cpu, REG(Instruction, RT), Rs + Imm);
}

static void CPU_J(CPU *Cpu, u32 Instruction)
{
    u32 Addr = (Cpu->NextInstructionPC & 0xF0000000) | (U26(Instruction) << 2);
    Cpu->PC = Addr;
    Cpu->Slot = SLOT_BRANCH;
}

static void CPU_Or(CPU *Cpu, u32 Instruction)
{
    u32 Rt = CPU_ReadReg(Cpu, REG(Instruction, RT));
    u32 Rs = CPU_ReadReg(Cpu, REG(Instruction, RS));
    CPU_WriteReg(Cpu, REG(Instruction, RD), Rt | Rs);
}

static void CPU_Bne(CPU *Cpu, u32 Instruction)
{
    u32 Rt = CPU_ReadReg(Cpu, REG(Instruction, RT));
    u32 Rs = CPU_ReadReg(Cpu, REG(Instruction, RS));
    if (Rt != Rs)
    {
        CPU_Branch(Cpu, Instruction);
    }
}

static void CPU_Addi(CPU *Cpu, u32 Instruction)
{
    u32 Imm = I16(Instruction);
    u32 Rs = CPU_ReadReg(Cpu, REG(Instruction, RS));
    if (OverflowOnAdd(Rs, Imm))
    {
        CPU_GenerateException(Cpu, CPU_EXCEPTION_OVERFLOW);
    }
    else
    {
        CPU_WriteReg(Cpu, REG(Instruction, RT), Rs + Imm);
    }
}

static void CPU_Lw(CPU *Cpu, u32 Instruction)
{
    if (Cpu->SR & (1 << 16)) /* cache isolate bit */
    {
        LOG("Load is ignored while cache is being isolated\n");
        return;
    }

    u32 Addr = CPU_ReadReg(Cpu, REG(Instruction, RS)) + I16(Instruction);
    if (Addr & 3)
    {
        CPU_GenerateException(Cpu, CPU_EXCEPTION_LOAD_ADDR_ERR);
    }
    else
    {
        CPU_SetDelayedLoad(Cpu, 
            REG(Instruction, RT), 
            PS1_Read32(Cpu->Bus, Addr)
        );
    }
}

static void CPU_Sltu(CPU *Cpu, u32 Instruction)
{
    u32 Rs = CPU_ReadReg(Cpu, REG(Instruction, RS));
    u32 Rt = CPU_ReadReg(Cpu, REG(Instruction, RT));
    CPU_WriteReg(Cpu, REG(Instruction, RD), Rs < Rt);
}

static void CPU_Addu(CPU *Cpu, u32 Instruction)
{
    u32 Rs = CPU_ReadReg(Cpu, REG(Instruction, RS));
    u32 Rt = CPU_ReadReg(Cpu, REG(Instruction, RT));
    CPU_WriteReg(Cpu, REG(Instruction, RD), Rs + Rt);
}

static void CPU_Sh(CPU *Cpu, u32 Instruction)
{
    u32 Addr = CPU_ReadReg(Cpu, REG(Instruction, RS)) + I16(Instruction);
    u32 Data = CPU_ReadReg(Cpu, REG(Instruction, RT));
    if (Addr & 1)
    {
        CPU_GenerateException(Cpu, CPU_EXCEPTION_STORE_ADDR_ERR);
    }
    else
    {
        PS1_Write16(Cpu->Bus, Addr, Data);
    }
}

static void CPU_Jal(CPU *Cpu, u32 Instruction)
{
    CPU_WriteReg(Cpu, 31, Cpu->PC);
    CPU_J(Cpu, Instruction);
    Cpu->Slot = SLOT_BRANCH;
}

static void CPU_Andi(CPU *Cpu, u32 Instruction)
{
    u32 Rs = CPU_ReadReg(Cpu, REG(Instruction, RS));
    u32 Imm = U16(Instruction);
    CPU_WriteReg(Cpu, REG(Instruction, RT), Rs & Imm);
}

static void CPU_Sb(CPU *Cpu, u32 Instruction)
{
    u32 Addr = CPU_ReadReg(Cpu, REG(Instruction, RS)) + I16(Instruction);
    u32 Rt = CPU_ReadReg(Cpu, REG(Instruction, RT));
    PS1_Write8(Cpu->Bus, Addr, Rt);
}

static void CPU_Jr(CPU *Cpu, u32 Instruction)
{
    u32 Rs = CPU_ReadReg(Cpu, REG(Instruction, RS));
    Cpu->PC = Rs;
    Cpu->Slot = SLOT_BRANCH;
}

static void CPU_Lb(CPU *Cpu, u32 Instruction)
{
    u32 Addr = CPU_ReadReg(Cpu, REG(Instruction, RS)) + I16(Instruction);
    i32 Data = (i8)PS1_Read8(Cpu->Bus, Addr);
    CPU_SetDelayedLoad(Cpu, 
        REG(Instruction, RT),
        Data
    );
}

static void CPU_Beq(CPU *Cpu, u32 Instruction)
{
    u32 Rs = CPU_ReadReg(Cpu, REG(Instruction, RS));
    u32 Rt = CPU_ReadReg(Cpu, REG(Instruction, RT));
    if (Rs == Rt)
    {
        CPU_Branch(Cpu, Instruction);
    }
}

static void CPU_And(CPU *Cpu, u32 Instruction)
{
    u32 Rs = CPU_ReadReg(Cpu, REG(Instruction, RS));
    u32 Rt = CPU_ReadReg(Cpu, REG(Instruction, RT));
    CPU_WriteReg(Cpu, REG(Instruction, RD), Rs & Rt);
}

static void CPU_Add(CPU *Cpu, u32 Instruction)
{
    u32 Rs = CPU_ReadReg(Cpu, REG(Instruction, RS));
    u32 Rt = CPU_ReadReg(Cpu, REG(Instruction, RT));
    if (OverflowOnAdd(Rs, Rt))
    {
        CPU_GenerateException(Cpu, CPU_EXCEPTION_OVERFLOW);
    }
    else
    {
        CPU_WriteReg(Cpu, REG(Instruction, RD), Rs + Rt);
    }
}

static void CPU_Bgtz(CPU *Cpu, u32 Instruction)
{
    u32 Rs = CPU_ReadReg(Cpu, REG(Instruction, RS));
    if ((i32)Rs > 0)
    {
        CPU_Branch(Cpu, Instruction);
    }
}

static void CPU_Blez(CPU *Cpu, u32 Instruction)
{
    u32 Rs = CPU_ReadReg(Cpu, REG(Instruction, RS));
    if ((i32)Rs <= 0)
    {
        CPU_Branch(Cpu, Instruction);
    }
}

static void CPU_Lbu(CPU *Cpu, u32 Instruction)
{
    u32 Addr = CPU_ReadReg(Cpu, REG(Instruction, RS)) + I16(Instruction);
    u8 Data = PS1_Read8(Cpu->Bus, Addr);

    CPU_SetDelayedLoad(Cpu, 
        REG(Instruction, RT), 
        Data
    );
}

static void CPU_Jalr(CPU *Cpu, u32 Instruction)
{
    u32 Rs = CPU_ReadReg(Cpu, REG(Instruction, RS));
    CPU_WriteReg(Cpu, REG(Instruction, RD), Cpu->PC);
    Cpu->PC = Rs;
    Cpu->Slot = SLOT_BRANCH;
}

static void CPU_Bcc(CPU *Cpu, u32 Instruction)
{
    /* link bit */
    if (Instruction & (1 << 20))
    {
        CPU_WriteReg(Cpu, 31, Cpu->PC);
    }

    u32 Rs = CPU_ReadReg(Cpu, REG(Instruction, RS));
    /* >= 0 bit */
    if (((Rs >> 31) ^ (Instruction >> 16)) & 1)
    {
        CPU_Branch(Cpu, Instruction);
    }
}

static void CPU_Slti(CPU *Cpu, u32 Instruction)
{
    i32 Rs = CPU_ReadReg(Cpu, REG(Instruction, RS));
    i32 Imm = I16(Instruction);
    CPU_WriteReg(Cpu, REG(Instruction, RT), Rs < Imm);
}

static void CPU_Subu(CPU *Cpu, u32 Instruction)
{
    u32 Rs = CPU_ReadReg(Cpu, REG(Instruction, RS));
    u32 Rt = CPU_ReadReg(Cpu, REG(Instruction, RT));
    CPU_WriteReg(Cpu, REG(Instruction, RD), Rs - Rt);
}

static void CPU_Sra(CPU *Cpu, u32 Instruction)
{
    i32 Rt = CPU_ReadReg(Cpu, REG(Instruction, RT));
    uint Shamt = SHAMT(Instruction);

    /*  Impl-defined pre C++20 (though most impl it as an arith shift right instruction) */
    /*  C++20 or above defined it as strictly arith shift right  */
    i32 Result = Rt >> Shamt;

    CPU_WriteReg(Cpu, REG(Instruction, RD), Result);
}

static void CPU_Div(CPU *Cpu, u32 Instruction)
{
    i32 Rs = CPU_ReadReg(Cpu, REG(Instruction, RS));
    i32 Rt = CPU_ReadReg(Cpu, REG(Instruction, RT));
    if (Rt == 0)
    {
        Cpu->Hi = Rs;
        Cpu->Lo = Rs < 0? 1 : -1;
    }
    else if ((u32)Rs == 0x80000000 && Rt == -1)
    {
        Cpu->Lo = 0x80000000;
        Cpu->Hi = 0;
    }
    else 
    {
        Cpu->Lo = Rs / Rt;
        Cpu->Hi = Rs % Rt;
    }
}

static void CPU_Mf(CPU *Cpu, u32 Instruction, u32 HiLo)
{
    if (Cpu->HiLoCyclesLeft)
    {
        Cpu->HiLoBlocking = true;
    }
    else
    {
        CPU_WriteReg(Cpu, REG(Instruction, RD), HiLo);
    }
}

static void CPU_Srl(CPU *Cpu, u32 Instruction)
{
    u32 Rt = CPU_ReadReg(Cpu, REG(Instruction, RT));
    uint Shamt = SHAMT(Instruction);
    CPU_WriteReg(Cpu, REG(Instruction, RD), Rt >> Shamt);
}

static void CPU_Sltiu(CPU *Cpu, u32 Instruction)
{
    u32 Rs = CPU_ReadReg(Cpu, REG(Instruction, RS));
    u32 Imm = I16(Instruction);
    CPU_WriteReg(Cpu, REG(Instruction, RT), Rs < Imm);
}

static void CPU_Divu(CPU *Cpu, u32 Instruction)
{
    u32 Rs = CPU_ReadReg(Cpu, REG(Instruction, RS));
    u32 Rt = CPU_ReadReg(Cpu, REG(Instruction, RT));
    if (Rt == 0)
    {
        Cpu->Hi = Rt;
        Cpu->Lo = (u32)-1;
    }
    else 
    {
        Cpu->Lo = Rs / Rt;
        Cpu->Hi = Rs % Rt;
    }
}

static void CPU_Slt(CPU *Cpu, u32 Instruction)
{
    i32 Rs = CPU_ReadReg(Cpu, REG(Instruction, RS));
    i32 Rt = CPU_ReadReg(Cpu, REG(Instruction, RT));
    CPU_WriteReg(Cpu, REG(Instruction, RD), Rs < Rt);
}

static u32 CPU_Mt(CPU *Cpu, u32 Instruction)
{
    return CPU_ReadReg(Cpu, REG(Instruction, RS));
}

static void CPU_Lhu(CPU *Cpu, u32 Instruction)
{
    u32 Addr = CPU_ReadReg(Cpu, REG(Instruction, RS)) + I16(Instruction);
    u16 Data = PS1_Read16(Cpu->Bus, Addr);
    if (Addr & 1)
    {
        CPU_GenerateException(Cpu, CPU_EXCEPTION_LOAD_ADDR_ERR);
    }
    else
    {
        CPU_WriteReg(Cpu, REG(Instruction, RT), Data);
    }
}

static void CPU_Sllv(CPU *Cpu, u32 Instruction)
{
    u32 ShiftCount = CPU_ReadReg(Cpu, REG(Instruction, RS)) & 0x1F;
    u32 Rt = CPU_ReadReg(Cpu, REG(Instruction, RT));
    CPU_WriteReg(Cpu, REG(Instruction, RD), Rt << ShiftCount);
}

static void CPU_Lh(CPU *Cpu, u32 Instruction)
{
    u32 Addr = CPU_ReadReg(Cpu, REG(Instruction, RS)) + I16(Instruction);
    i32 Data = (i16)PS1_Read16(Cpu->Bus, Addr);
    if (Addr & 1)
    {
        CPU_GenerateException(Cpu, CPU_EXCEPTION_LOAD_ADDR_ERR);
    }
    else
    {
        CPU_WriteReg(Cpu, REG(Instruction, RT), Data);
    }
}

static void CPU_Nor(CPU *Cpu, u32 Instruction)
{
    u32 Rs = CPU_ReadReg(Cpu, REG(Instruction, RS));
    u32 Rt = CPU_ReadReg(Cpu, REG(Instruction, RT));
    CPU_WriteReg(Cpu, REG(Instruction, RD), ~(Rs | Rt));
}

static void CPU_Srav(CPU *Cpu, u32 Instruction)
{
    u32 ShiftCount = 0x1F & CPU_ReadReg(Cpu, REG(Instruction, RS));
    i32 Rt = CPU_ReadReg(Cpu, REG(Instruction, RT));
    CPU_WriteReg(Cpu, REG(Instruction, RD), 
        Rt < 0? ~(~Rt) >> ShiftCount : Rt >> ShiftCount
    );
}

static void CPU_Srlv(CPU *Cpu, u32 Instruction)
{
    u32 ShiftCount = 0x1F & CPU_ReadReg(Cpu, REG(Instruction, RS));
    u32 Rt = CPU_ReadReg(Cpu, REG(Instruction, RT));
    CPU_WriteReg(Cpu, REG(Instruction, RD), Rt >> ShiftCount);
}

static void CPU_Multu(CPU *Cpu, u32 Instruction)
{
    u32 Rs = CPU_ReadReg(Cpu, REG(Instruction, RS));
    u32 Rt = CPU_ReadReg(Cpu, REG(Instruction, RT));
    u64 Result = (u64)Rs * (u64)Rt;

    Cpu->Lo = (u32)Result;
    Cpu->Hi = (u32)(Result >> 32);
}

static void CPU_Xor(CPU *Cpu, u32 Instruction)
{
    u32 Rs = CPU_ReadReg(Cpu, REG(Instruction, RS));
    u32 Rt = CPU_ReadReg(Cpu, REG(Instruction, RT));
    CPU_WriteReg(Cpu, REG(Instruction, RD), Rs ^ Rt);
}

static void CPU_Mult(CPU *Cpu, u32 Instruction)
{
    i32 Rs = CPU_ReadReg(Cpu, REG(Instruction, RS));
    i32 Rt = CPU_ReadReg(Cpu, REG(Instruction, RT));
    i64 Result = (i64)Rs * (i64)Rt;
    Cpu->Lo = (u32)Result;
    Cpu->Hi = (u32)(Result >> 32);
}

static void CPU_Sub(CPU *Cpu, u32 Instruction)
{
    u32 Rs = CPU_ReadReg(Cpu, REG(Instruction, RS));
    u32 Rt = CPU_ReadReg(Cpu, REG(Instruction, RT));
    if (OverflowOnSub(Rs, Rt))
    {
        CPU_GenerateException(Cpu, CPU_EXCEPTION_OVERFLOW);
    }
    else
    {
        CPU_WriteReg(Cpu, REG(Instruction, RD), Rs - Rt);
    }
}

static void CPU_Xori(CPU *Cpu, u32 Instruction)
{
    u32 Rs = CPU_ReadReg(Cpu, REG(Instruction, RS));
    u32 Imm16 = U16(Instruction);
    CPU_WriteReg(Cpu, REG(Instruction, RT), Rs ^ Imm16);
}


/* load word to the left part of a register (on a register diagram) 
 * Addr = 2:
 * register diagram: 
 * bits:        31                8     0
 * reg:         |/////////////////|     |
 * mem bits:    23                0
 *
 * memory diagram:
 * addr:        |  0  |  1  |  2  |  3  |
 * bits:        0                23    31
 * data:        |/////////////////|     |
 */
static void CPU_Lwl(CPU *Cpu, u32 Instruction)
{
    u32 Addr = CPU_ReadReg(Cpu, REG(Instruction, RS)) + I16(Instruction);
    uint RtIndex = REG(Instruction, RT);
    u32 RtData = Cpu->OutR[RtIndex];

    /* do an aligned load */
    u32 AlignedAddr = Addr & ~3;
    u32 AlignedData = PS1_Read32(Cpu->Bus, AlignedAddr);

    /* move data appropriately to rt */
    uint ShiftCount = Addr & 3;
    u32 Data = 
        (RtData & (0x00FFFFFF >> ShiftCount*8))
        | (AlignedData << (3 - ShiftCount)*8);

    CPU_SetDelayedLoad(Cpu, RtIndex, Data);
}

/* load word to the right part of a register (on a register diagram) 
 * Addr = 2:
 * register diagram: 
 * bits:        31          15          0
 * reg:         |           |///////////|
 * mem bits:                16         31
 *
 * memory diagram: 
 * addr:        |  0  |  1  |  2  |  3  |
 * bits:        0           16         31
 * data:        |           |///////////|
 */
static void CPU_Lwr(CPU *Cpu, u32 Instruction)
{
    u32 Addr = CPU_ReadReg(Cpu, REG(Instruction, RS)) + I16(Instruction);
    uint RtIndex = REG(Instruction, RT);
    u32 RtData = Cpu->OutR[RtIndex];

    /* do an aligned load */
    u32 AlignedAddr = Addr & ~3;
    u32 AlignedData = PS1_Read32(Cpu->Bus, AlignedAddr);

    /* move data appropriately to rt */
    uint ShiftCount = Addr & 3;
    u32 Data = 
        (RtData & (0xFFFFFF00 << (3 - ShiftCount)*8))
        | (AlignedData >> (ShiftCount)*8);

    CPU_SetDelayedLoad(Cpu, RtIndex, Data);
}

/* store word to the left part of memory (on a little endian memory diagram)
 * Addr = 2:
 * bits:        31                8     0
 * reg:         |/////////////////|     |
 *
 * addr:        |  0  |  1  |  2  |  3  |
 * mem bits:    0                 24    31
 * data:        |/////////////////|     |
 * reg bits:    8                31
 */
static void CPU_Swl(CPU *Cpu, u32 Instruction)
{
    u32 Addr = CPU_ReadReg(Cpu, REG(Instruction, RS)) + I16(Instruction);
    u32 Rt = CPU_ReadReg(Cpu, REG(Instruction, RT));

    u32 AlignedAddr = Addr & ~3;
    u32 PrevData = PS1_Read32(Cpu->Bus, AlignedAddr);

    uint ShiftCount = Addr & 3;
    u32 Data = 
        (PrevData & (0xFFFFFF00 << (ShiftCount)*8))
        | (Rt >> (3 - ShiftCount)*8);

    PS1_Write32(Cpu->Bus, AlignedAddr, Data);
}

/* store word to the right part of memory (on a little endian memory diagram)
 * Addr = 2
 * register diagram: 
 * bits:        31          15          0
 * reg:         |           |///////////|
 *
 * memory diagram:
 * addr:        |  0  |  1  |  2  |  3  |
 * bits:        0           16         31
 * data:        |           |///////////|
 * reg bits:                0          15
 */
static void CPU_Swr(CPU *Cpu, u32 Instruction)
{
    u32 Addr = CPU_ReadReg(Cpu, REG(Instruction, RS)) + I16(Instruction);
    u32 Rt = CPU_ReadReg(Cpu, REG(Instruction, RT));

    u32 AlignedAddr = Addr & ~3;
    u32 PrevData = PS1_Read32(Cpu->Bus, AlignedAddr);

    uint ShiftCount = Addr & 3;
    u32 Data = 
        (PrevData & (0x00FFFFFF >> (3 - ShiftCount)*8)) 
        | (Rt << (ShiftCount)*8);

    PS1_Write32(Cpu->Bus, AlignedAddr, Data);
}

static void CPU_LwC2(CPU *Cpu, u32 Instruction)
{
    (void)Cpu, (void)Instruction;
    TODO("lwc2");
}

static void CPU_SwC2(CPU *Cpu, u32 Instruction)
{
    (void)Cpu, (void)Instruction;
    TODO("swc2");
}





static void CPU_Branch(CPU *Cpu, u32 Instruction)
{
    i32 Offset = I16(Instruction) << 2;
    u32 NewPC = Cpu->NextInstructionPC + Offset; /* -4 to compensate for increment in Clock() */
    Cpu->PC = NewPC;
    Cpu->Slot = SLOT_BRANCH;
}



static Bool8 OverflowOnAdd(u32 A, u32 B)
{
    u32 Sum = A + B;
    /*  Overflowed = sign(A) == sign(B) && sign(A) != sign(Sum)  */
    Bool8 Overflowed = (~(A ^ B) & (A ^ Sum)) >> 31;
    return Overflowed;
}

static Bool8 OverflowOnSub(u32 A, u32 B)
{
    u32 R = A - B;
    Bool8 Overflowed = ((A ^ B) & (A ^ R)) >> 31;
    return Overflowed;
}



static void CPU_Cop0_Mtc(CPU *Cpu, u32 Instruction)
{
    u32 Rt = CPU_ReadReg(Cpu, REG(Instruction, RT));
    switch (REG(Instruction, RD))
    {
    case 12: /*  Status Register (SR)  */
    {
        Cpu->SR = Rt;
    } break;
    case 13: /*  Cause Register */
    {
        Cpu->Cause = Rt;
    } break;
    case 3: /*  BPC  */
    case 5: /*  BDA  */
    case 6: /*  ??? */
    case 7: /*  DCIC */
    case 9: /*  BDAM */
    case 11: /*  BPCM */
    {
        if (Rt != 0)
        {
            TODO("nonzero write to breakpoint registers");
        }
    } break;
    default:
    {
        TODO("MTC0 writing to cop0 R%d", REG(Instruction, RD));
    } break;
    }
}

static void CPU_Cop0_Mfc(CPU *Cpu, u32 Instruction)
{
    u32 CopReg = 0;
    switch (REG(Instruction, RD))
    {
    case 12: /* SR */
    {
        CopReg = Cpu->SR;
    } break;
    case 13: /* Cause */
    {
        CopReg = Cpu->Cause;
    } break;
    case 14: /* EPC */
    {
        CopReg = Cpu->EPC;
    } break;
    default:
    {
        TODO("Reading from R%d of COP0", REG(Instruction, RD));
    } break;
    }

    CPU_SetDelayedLoad(Cpu, 
        REG(Instruction, RT),
        CopReg
    );
}

static void CPU_Cop0_Rfe(CPU *Cpu, u32 Instruction)
{
    (void)Instruction;
    uint ExceptionModeStack = Cpu->SR & 0x3F;
    ExceptionModeStack >>= 2;
    Cpu->SR = (Cpu->SR & ~0x3F) | (ExceptionModeStack & 0x3F);
}




