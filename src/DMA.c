
#include "Ps1.h"


static inline Bool8 DMA_IsChanelActive(const DMA_ChanelCtrl *Chanel)
{
    /* only need to check trigger bit when sync mode is 0 (manual) */
    Bool8 TriggerSet = 
        Chanel->SyncMode != DMA_SYNCMODE_MANUAL
        || Chanel->ManualTrigger;
    return Chanel->Enable && TriggerSet;
}


void DMA_Reset(DMA *Dma, PS1 *Bus)
{
    *Dma = (DMA) {
        .PriorityCtrlReg = 0x07654321, /* default priority */
        .Bus = Bus,
        .Chanels[DMA_PORT_OTC].Ctrl.Decrement = 1, /* OTC always decrement */
    };
}

u32 DMA_Read32(DMA *Dma, u32 Offset)
{
    u32 Data = 0;
    u32 Major = (Offset >> 4) & 0x7;
    u32 Minor = (Offset & 0xF);
    if (Major == 7) /* master ctrl regs */
    {
        switch (Minor)
        {
        case 0: /* Priority Ctrl */
        {
            Data = Dma->PriorityCtrlReg;
        } break;
        case 4: /* Interrupt Ctrl */
        {
            Data = Dma->InterruptCtrlReg.Unknown
                | (u32)Dma->InterruptCtrlReg.ForceIRQ << 15
                | (u32)Dma->InterruptCtrlReg.IRQChanelEnable << 16 
                | (u32)Dma->InterruptCtrlReg.IRQMasterEnable << 23 
                | (u32)Dma->InterruptCtrlReg.IRQChanelFlags << 24 
                | (u32)Dma->InterruptCtrlReg.IRQMasterFlag << 31;
        } break;
        }
    }
    else /* per channel regs (0..6) */
    {
        DMA_Port Port = Major;
        const DMA_Chanel *Chanel = &Dma->Chanels[Port];
        switch (Minor)
        {
        case 0: /* Base Addr Regs */
        {
            Data = Chanel->BaseAddr;
        } break;
        case 4: /* Block Ctrl Regs */
        {
            Data = (u32)Chanel->BlockSize
                | (u32)Chanel->BlockAmount << 16;
        } break;
        case 8: /* Chanel Ctrl Regs */
        {
            Data = Chanel->Ctrl.RamToDevice
                | (u32)Chanel->Ctrl.Decrement << 1
                | (u32)Chanel->Ctrl.ChoppingEnable << 2
                | (u32)Chanel->Ctrl.SyncMode << 9
                | (u32)Chanel->Ctrl.ChoppingDMAWindow << 16 
                | (u32)Chanel->Ctrl.ChoppingCPUWindow << 20 
                | (u32)Chanel->Ctrl.Enable << 24 
                | (u32)Chanel->Ctrl.ManualTrigger << 28 
                | (u32)Chanel->Ctrl.Unknown << 29;
        } break;
        default:
        {
            TODO("DMA read32: offset 0x%0x", Offset);
        } break;
        }
    }
    return Data;
}

void DMA_Write32(DMA *Dma, u32 Offset, u32 Data)
{
    u32 Major = (Offset >> 4) & 0x7;
    u32 Minor = Offset & 0xF;
    if (Major == 7) /* master ctrl regs */
    {
        switch (Minor)
        {
        case 0: /* Priority Ctrl */
        {
            Dma->PriorityCtrlReg = Data;
        } break;
        case 4: /* Interrupt Ctrl */
        {
            Dma->InterruptCtrlReg.Unknown = Data;
            Dma->InterruptCtrlReg.ForceIRQ = Data >> 15;
            Dma->InterruptCtrlReg.IRQChanelEnable = Data >> 16;
            Dma->InterruptCtrlReg.IRQMasterEnable = Data >> 23;

            /* writing 1 to a flag resets it, flag value stays otherwise */
            Dma->InterruptCtrlReg.IRQChanelFlags &= ~(Data >> 24);

#if 0 /* TODO: move this somwhere else? */
            Dma->InterruptCtrlReg.IRQMasterFlag = 
                Dma->InterruptCtrlReg.ForceIRQ 
                || (Dma->InterruptCtrlReg.IRQMasterEnable && Dma->InterruptCtrlReg.IRQChanelFlags);
#endif 
        } break;
        default:
        {
            TODO("DMA write32: offset 0x%x <- %08x", Offset, Data);
        } break;
        }
    }
    else /* per channel regs */
    {
        DMA_Port Port = Major;
        DMA_Chanel *Chanel = &Dma->Chanels[Port];
        switch (Minor)
        {
        case 0: /* Base Addr Regs */
        {
            Chanel->BaseAddr = Data & 0xFFFFFF;
        } break;
        case 4: /* Block Ctrl Regs */
        {
            Chanel->BlockSize = Data & 0xFFFF;
            Chanel->BlockAmount = Data >> 16;
        } break;
        case 8: /* Chanel Ctrl Regs */
        {
            if (Port == DMA_PORT_OTC) /* only bits 24, 28, 30 are R/W */
            {
                Chanel->Ctrl.Enable = Data >> 24;
                Chanel->Ctrl.ManualTrigger = Data >> 28;
                Chanel->Ctrl.Unknown = (Data >> 29) & 0x2;
            }
            else
            {
                Chanel->Ctrl.RamToDevice = Data;
                Chanel->Ctrl.Decrement = Data >> 1;
                Chanel->Ctrl.ChoppingEnable = Data >> 2;
                Chanel->Ctrl.SyncMode = Data >> 9;
                Chanel->Ctrl.ChoppingDMAWindow = Data >> 16;
                Chanel->Ctrl.ChoppingCPUWindow = Data >> 20;
                Chanel->Ctrl.Enable = Data >> 24;
                Chanel->Ctrl.ManualTrigger = Data >> 28;
                Chanel->Ctrl.Unknown = Data >> 29;
            }

            if (DMA_IsChanelActive(&Chanel->Ctrl))
            {
                PS1_DoDMATransfer(Dma->Bus, Port);
            }
        } break;
        default:
        {
            TODO("DMA write32: offset 0x%x <- %08x", Offset, Data);
        } break;
        }
    }
}

u32 DMA_GetChanelTransferSize(const DMA_Chanel *Chanel)
{
    switch ((DMA_SyncMode)Chanel->Ctrl.SyncMode)
    {
    case DMA_SYNCMODE_MANUAL:
    {
        return Chanel->BlockSize;
    } break;
    case DMA_SYNCMODE_REQUEST:
    {
        return (u32)Chanel->BlockAmount * (u32)Chanel->BlockSize;
    } break;
    default:
    case DMA_SYNCMODE_LINKEDLIST:
    {
        UNREACHABLE("linked list mode does not have transfer size");
        return 0;
    } break;
    }
}

void DMA_SetTransferFinishedState(DMA_Chanel *Chanel)
{
    Chanel->Ctrl.Enable = 0;
    Chanel->Ctrl.ManualTrigger = 0;
    /* TODO: also set interrupt */
}



