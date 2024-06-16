#ifndef CPU_H
#define CPU_H

#include "Common.h"


typedef enum 
{
    CPU_EXCEPTION_LOAD_ADDR_ERR = 0x4,
    CPU_EXCEPTION_STORE_ADDR_ERR = 0x5,
    CPU_EXCEPTION_SYSCALL = 0x8,
    CPU_EXCEPTION_BREAK = 0x9,
    CPU_EXCEPTION_ILLEGAL_INS = 0xA,
    CPU_EXCEPTION_OVERFLOW = 0xC,
    CPU_EXCEPTION_COP_ERR = 0xD,
} CPU_Exception;


typedef struct
{
    PS1* Bus;
    /*  current PC            delay slot PC      after delay slot PC */
    u32 CurrentInstructionPC, NextInstructionPC, PC;
    u32 CurrentInstruction;

    u32 R[32];
    u32 OutR[32];
    u32 LoadValue;
    u32 LoadIndex;
    uint HiLoCyclesLeft;
    Bool8 HiLoBlocking;
    u32 Hi;
    u32 Lo;

    u32 Cause;
    u32 SR;
    u32 EPC;

    unsigned Slot:2;
} CPU;

void CPU_Reset(CPU *Cpu, PS1 *Bus);
void CPU_Clock(CPU *Cpu);
void CPU_DecodeExecute(CPU *Cpu, u32 Instruction);
void CPU_GenerateException(CPU *Cpu, CPU_Exception Exception);


#endif /* CPU_H */

