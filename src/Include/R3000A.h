#ifndef R3000A_H
#define R3000A_H

#include "Common.h"
#include "CP0.h"

typedef enum R3000A_DataSize 
{
    DATA_BYTE = sizeof(u8),
    DATA_HALF = sizeof(u16),
    DATA_WORD = sizeof(u32),
} R3000A_DataSize;

typedef void (*R3000AWrite)(void *UserData, u32 Addr, u32 Data, R3000A_DataSize Size);
typedef u32 (*R3000ARead)(void *UserData, u32 Addr, R3000A_DataSize Size);
typedef Bool8 (*R3000AAddrVerifyFn)(void *UserData, u32 Addr);

#define R3000A_PIPESTAGE_COUNT 5
typedef struct R3000A 
{
    u32 R[32];
    u32 Hi, Lo;

    u32 PC;
    u32 PCSave[R3000A_PIPESTAGE_COUNT];
    u32 Instruction[R3000A_PIPESTAGE_COUNT];
    Bool8 InstructionIsBranch[R3000A_PIPESTAGE_COUNT];

    int PipeStage;
    int HiLoCyclesLeft;
    Bool8 HiLoBlocking;
    Bool8 ExceptionRaised;
    int ExceptionCyclesLeft;

    void *UserData;
    R3000ARead ReadFn;
    R3000AWrite WriteFn;
    R3000AAddrVerifyFn VerifyInstructionAddr;
    R3000AAddrVerifyFn VerifyDataAddr;

    R3000A_CP0 CP0;
} R3000A;

R3000A R3000A_Init(
    void *UserData, 
    R3000ARead ReadFn, R3000AWrite WriteFn,
    R3000AAddrVerifyFn DataAddrVerifier, R3000AAddrVerifyFn InstructionAddrVerifier
);
void R3000A_StepClock(R3000A *This);
void R3000A_Reset(R3000A *This);


#endif /* R3000A_H */
