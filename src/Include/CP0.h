#ifndef CP0_H
#define CP0_H

#include "Common.h"

typedef enum R3000A_Exception 
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
} R3000A_Exception;

#define R3000A_RESET_VEC 0xBFC00000
#define CP0_STATUS_BEV (1 << 22)
typedef struct R3000A_CP0
{
    u32 BPC;                /* breakpoint on execute */

    u32 BDA;                /* breakpoint on data access */
    u32 JumpDest;           /* random memorized jump addr */
    u32 DCIC;               /* breakpoint ctrl */

    u32 BadVAddr;           /* bad virtual addr */
    u32 BDAM;               /* data access breakpoint mask */
    u32 BPCM;               /* execute breakpoint mask */

    u32 Status;
    u32 Cause;
    u32 EPC;
    u32 PrID;
} R3000A_CP0;

R3000A_CP0 CP0_Init(u32 PrID);
void CP0_SetException(
    R3000A_CP0 *This, 
    R3000A_Exception Exception,
    u32 EPC, 
    u32 Instruction, 
    Bool8 BranchDelay, 
    u32 Addr
);
Bool8 CP0_IsCoprocessorAvailable(R3000A_CP0 *This, uint CoprocessorNumber);
u32 CP0_GetExceptionVector(const R3000A_CP0 *This);

u32 CP0_Read(const R3000A_CP0 *This, uint RegIndex);
void CP0_Write(R3000A_CP0 *This, u32 RdIndex, u32 Data);

#endif /* CP0_H */

