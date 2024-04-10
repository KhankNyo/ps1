#ifndef CP0_C
#define CP0_C
#include "CP0.h"


R3000A_CP0 CP0_Init(u32 PrID)
{
    R3000A_CP0 This = {
        .PrID = PrID,
    };
    return This;
}

void CP0_SetException(
    R3000A_CP0 *This, 
    R3000A_Exception Exception,
    u32 EPC, 
    u32 Instruction, 
    Bool8 BranchDelay, 
    u32 Addr)
{
    switch (Exception)
    {
    case EXCEPTION_ADEL:
    case EXCEPTION_ADES:
    {
        This->BadVAddr = Addr;
    } break;
    default: break;
    }

    This->EPC = EPC;

    /* write exception code to Cause register */
    MASKED_LOAD(This->Cause, Exception << 2, 0x1F << 2);
    /* write BD bit */
    BranchDelay = 0 != BranchDelay;
    MASKED_LOAD(This->Cause, (u32)BranchDelay << 31, 1ul << 31);
    /* write bit 26..27 to bits 28..29 */
    MASKED_LOAD(This->Cause, Instruction << 2, 0x3 << 28);

    /* pushes new kernel mode and interrupt flag
     * bit 1 = 0 for kernel mode, 
     * bit 0 = 0 for interrupt disable */
    u32 NewStatusStack = (This->Status & 0xF) << 2 | 0x00;
    MASKED_LOAD(This->Status, NewStatusStack, 0x3F);
}


u32 CP0_GetExceptionVector(const R3000A_CP0 *This)
{
    R3000A_Exception Exception = (This->Cause >> 2) & 0x1F;
    Bool8 UseBootExceptionVec = 0 != (This->Status & CP0_STATUS_BEV);
    switch (Exception)
    {
    case EXCEPTION_BP: /* debug break exception */
    {
        if (UseBootExceptionVec)
            return 0xBFC0140;
        return 0x80000040;
    } break;
    default: /* general exceptions like interrupt, arith, ... */
    {
        if (UseBootExceptionVec)
            return 0xBFC00180;
        return 0x80000080;
    } break;
    }
}



u32 CP0_Read(const R3000A_CP0 *This, uint RegIndex)
{
    switch (RegIndex)
    {
    case 3: return This->BPC;
    case 5: return This->BDA;
    case 6: return This->JumpDest;
    case 7: return This->DCIC;
    case 8: return This->BadVAddr;
    case 9: return This->BDAM;
    case 11: return This->BPCM;
    case 12: return This->Status;
    case 13: return This->Cause;
    case 14: return This->EPC;
    case 15: return This->PrID;
    default: return 0;
    }
}

void CP0_Write(R3000A_CP0 *This, u32 RdIndex, u32 Data)
{
    switch (RdIndex)
    {
    case 3: This->BPC = Data; break;
    case 5: This->BDA = Data; break;
    case 7: This->DCIC = Data; break;
    case 9: This->BDAM = Data; break;
    case 11: This->BPCM = Data; break;
    case 12: This->Status = Data; break;    
    case 13: MASKED_LOAD(This->Cause, Data, 0x3 << 8); break; /* write to bits 8..9 only */
    }

}

Bool8 CP0_IsCoprocessorAvailable(R3000A_CP0 *This, uint CoprocessorNumber)
{
    if (CoprocessorNumber == 1 || CoprocessorNumber == 3)
        return false;

    Bool8 InKernelMode = This->Status & (1 << 1);
    if (InKernelMode && CoprocessorNumber == 0)
        return true;

    Bool8 CoprocessorIsAvailable = (This->Status >> (28 + CoprocessorNumber)) & 1;
    return CoprocessorIsAvailable;
}


#endif /* CP0_C */

