#include <stdio.h>
#include <string.h> /* memset, memcpy */

#include "Common.h"
#include "CPU.h"
#include "Ps1.h"



void DMA_Reset(DMA *Dma, PS1 *Bus)
{
    *Dma = (DMA) {
        .PriorityCtrlReg = 0x07654321,
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

void PS1_Reset(PS1 *Ps1)
{
    CPU_Reset(&Ps1->Cpu, Ps1);
    DMA_Reset(&Ps1->Dma, Ps1);
}

static void PS1_DoDMATransferBlock(PS1 *Ps1, DMA_Port Port)
{
    DMA_Chanel *Chanel = &Ps1->Dma.Chanels[Port];
    int Increment = 
        Chanel->Ctrl.Decrement
        ? -4 : 4;
    u32 Addr = Chanel->BaseAddr;
    u32 WordsLeft = DMA_GetChanelTransferSize(Chanel);
    if (Chanel->Ctrl.RamToDevice)
    {
        LOG("[DMA Transfer]: ram to device(%d):\n"
            "    Addr: %08x..%08x\n"
            "    Incr: %d\n"
            "    Size: %08x (%d) words\n",
            Port, 
            Addr, Addr + Increment*WordsLeft,
            Increment,
            WordsLeft, WordsLeft
        );
        do {
            u32 CurrentAddr = (Addr % PS1_RAM_SIZE) & ~0x3;
            u32 Data;
            PS1_Ram_Read32(Ps1, CurrentAddr, &Data);
            switch (Port)
            {
            case DMA_PORT_GPU:
            {
                //LOG("      | %08x\n", Data);
            } break;
            default:
            {
                TODO("DMA for ram to device %d", Port);
            } break;
            }

            Addr += Increment;
            WordsLeft--;
        } while (WordsLeft != 0);
    }
    else /* device to ram */
    {
        LOG("[DMA Transfer]: device(%d) to ram:\n"
            "    Addr: %08x..%08x\n"
            "    Incr: %d\n"
            "    Size: %08x (%d) words\n",
            Port, 
            Addr, Addr + Increment*WordsLeft,
            Increment,
            WordsLeft, WordsLeft
        );
        do {
            /* wrap addr to ram size, ignore 2 LSB's */
            u32 CurrentAddr = (Addr % PS1_RAM_SIZE) & ~0x3;

            u32 SrcWord = 0;
            switch (Port)
            {
            case DMA_PORT_OTC:
            {
                /* current entry = prev entry */
                SrcWord = (Addr - 4) % PS1_RAM_SIZE;
                if (WordsLeft == 1) /* last entry */
                    SrcWord = 0xFFFFFF;
            } break;
            default:
            {
                TODO("DMA device %d to ram", Port);
            } break;
            }
            PS1_Ram_Write32(Ps1, CurrentAddr, SrcWord);

            Addr += Increment;
            WordsLeft--;
        } while (WordsLeft != 0);
    }

    DMA_SetTransferFinishedState(Chanel);
}

static void PS1_DoDMATransferLinkedList(PS1 *Ps1, DMA_Port Port)
{
    DMA_Chanel *Chanel = &Ps1->Dma.Chanels[Port];
    ASSERT(Port == DMA_PORT_GPU && "is this ok?");
    if (Chanel->Ctrl.RamToDevice == 0)
    {
        TODO("Invalid transfer direction for linked list mode: device to ram\n");
    }

    u32 Addr = (Chanel->BaseAddr % PS1_RAM_SIZE) & ~0x3;
    LOG("[DMA Transfer]: linked-list @ %08x\n", Addr);
    while (1)
    {
        u32 Header;
        PS1_Ram_Read32(Ps1, Addr, &Header);

        uint SizeWords = Header >> 24;
        for (uint WordCount = SizeWords; WordCount; WordCount--)
        {
            Addr = (Addr + sizeof(u32)) % PS1_RAM_SIZE;
            u32 GPUCommand;
            PS1_Ram_Read32(Ps1, Addr, &GPUCommand);
            LOG("    GPUCMD: %08x\n", GPUCommand);
        }

        /* last packet, low 24 bits are set to 1, but we only check the msb, 
         * that's how mednafen does the check 
         * (probably how the hardware does it too? Or just optimization?) */
        if (Header & 0x800000)
            break;

        Addr = (Header % PS1_RAM_SIZE) & ~0x3;
    }

    DMA_SetTransferFinishedState(Chanel);
}

void PS1_DoDMATransfer(PS1 *Ps1, DMA_Port Port)
{
    DMA_SyncMode SyncMode = Ps1->Dma.Chanels[Port].Ctrl.SyncMode; 
    switch (SyncMode)
    {
    case DMA_SYNCMODE_MANUAL:
    case DMA_SYNCMODE_REQUEST:
    {
        PS1_DoDMATransferBlock(Ps1, Port);
    } break;
    case DMA_SYNCMODE_LINKEDLIST:
    {
        PS1_DoDMATransferLinkedList(Ps1, Port);
    } break;
    default:
    {
        UNREACHABLE("unknown syncmode: %d", SyncMode);
    } break;
    }
}



typedef struct 
{
    Bool8 Valid;
    u32 Offset;
} TranslatedAddr;

static u32 PS1_GetPhysicalAddr(u32 LogicalAddr)
{
    static const u32 PS1_REGION_MASK_LUT[8] = {
        0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, /*  KUSEG,          0..0x7FFFFFFF */
        0x7FFFFFFF,                                     /*  KSEG0, 0x80000000..0x9FFFFFFF */
        0x1FFFFFFF,                                     /*  KSEG1, 0xA0000000..0xBFFFFFFF */
        0xFFFFFFFF, 0xFFFFFFFF                          /*  KSEG2, 0xC0000000..0xFFFFFFFF */
    };
    u32 MaskIndex = LogicalAddr >> 29;
    return PS1_REGION_MASK_LUT[MaskIndex] & LogicalAddr;
}

static TranslatedAddr InBiosRange(u32 PhysicalAddr)
{
    TranslatedAddr Translation = {
        .Valid = IN_RANGE(0x1FC00000, PhysicalAddr, 0x1FC00000 + PS1_BIOS_SIZE),
        .Offset = PhysicalAddr - 0x1FC00000,
    };
    return Translation;
}

static TranslatedAddr InMemCtrl1Range(u32 PhysicalAddr)
{
    TranslatedAddr Translation = {
        .Valid = IN_RANGE(0x1F801000, PhysicalAddr, 0x1F801020),
        .Offset = PhysicalAddr - 0x1F801000,
    };
    return Translation;
}

static TranslatedAddr InMemCtrl2Range(u32 PhysicalAddr)
{
    TranslatedAddr Translation = {
        .Valid = IN_RANGE(0x1F801060, PhysicalAddr, 0x1F80106F),
        .Offset = PhysicalAddr - 0x1F801060,
    };
    return Translation;
}

static TranslatedAddr InCacheCtrlRange(u32 PhysicalAddr)
{
    TranslatedAddr Translation = {
        .Valid = 0xFFFE0130 == PhysicalAddr,
        .Offset = PhysicalAddr - 0xFFFE0130,
    };
    return Translation;
}

static TranslatedAddr InRamRange(u32 PhysicalAddr)
{
    TranslatedAddr Translation = {
        .Valid = PhysicalAddr < PS1_RAM_SIZE,
        .Offset = PhysicalAddr,
    };
    return Translation;
}

static TranslatedAddr InSPURange(u32 PhysicalAddr)
{
    TranslatedAddr Translation = {
        .Valid = IN_RANGE(0x1F801C00, PhysicalAddr, 0x1F801FFF),
        .Offset = PhysicalAddr - 0x1F801C00,
    };
    return Translation;
}

static TranslatedAddr InExpansion2Range(u32 PhysicalAddr)
{
    TranslatedAddr Translation = {
        .Valid = IN_RANGE(0x1F802000, PhysicalAddr, 0x1F802000 + 8*KB),
        .Offset = PhysicalAddr - 0x1F802000,
    };
    return Translation;
}

static TranslatedAddr InExpansion1Range(u32 PhysicalAddr)
{
    TranslatedAddr Translation = {
        .Valid = IN_RANGE(0x1F000000, PhysicalAddr, 0x1F000000 + 1*KB),
        .Offset = PhysicalAddr - 0x1F000000,
    };
    return Translation;
}

static TranslatedAddr InInterruptCtrlRange(u32 PhysicalAddr)
{
    TranslatedAddr Translation = {
        .Valid = IN_RANGE(0x1F801070, PhysicalAddr, 0x1F801070 + 8),
        .Offset = PhysicalAddr - 0x1F801070,
    };
    return Translation;
}

static TranslatedAddr InTimerRange(u32 PhysicalAddr)
{
    TranslatedAddr Translation = {
        .Valid = IN_RANGE(0x1F801100, PhysicalAddr, 0x1F80112F),
        .Offset = PhysicalAddr - 0x1F801100,
    };
    return Translation;
}

static TranslatedAddr InDMARange(u32 PhysicalAddr)
{
    TranslatedAddr Translation = {
        .Valid = IN_RANGE(0x1F801080, PhysicalAddr, 0x1F8010FF),
        .Offset = PhysicalAddr - 0x1F801080,
    };
    return Translation;
}

static TranslatedAddr InGPURange(u32 PhysicalAddr)
{
    TranslatedAddr Translation = {
        .Valid = IN_RANGE(0x1F801810, PhysicalAddr, 0x1F801817),
        .Offset = PhysicalAddr - 0x1F801810,
    };
    return Translation;
}



u32 PS1_Read32(PS1 *Ps1, u32 LogicalAddr)
{
    if (LogicalAddr & 3)
    {
        TODO("unaligned load32: %08x", LogicalAddr);
    }

    const char *RegionName = "Unknown";
    u32 Data = 0;
    u32 PhysicalAddr = PS1_GetPhysicalAddr(LogicalAddr);
    TranslatedAddr Translation = InBiosRange(PhysicalAddr);
    if (Translation.Valid)
    {
        u32 Offset = Translation.Offset;
        ASSERT(Offset + 4 <= PS1_BIOS_SIZE && Offset < Offset + 4);

        Data = Ps1->Bios[Offset + 0];
        Data |= (u32)Ps1->Bios[Offset + 1] << 8;
        Data |= (u32)Ps1->Bios[Offset + 2] << 16;
        Data |= (u32)Ps1->Bios[Offset + 3] << 24;
        return Data; /* no log */
    }
    else if ((Translation = InRamRange(PhysicalAddr)).Valid)
    {
        u32 Offset = Translation.Offset;
        ASSERT(Offset + 4 <= PS1_RAM_SIZE && Offset < Offset + 4);

        PS1_Ram_Read32(Ps1, Offset, &Data);
        return Data; /* no log */
    }
    else if ((Translation = InMemCtrl1Range(PhysicalAddr)).Valid)
    {
        TODO("Handle reading memctrl1: %08x\n", LogicalAddr);
    }
    else if ((Translation = InMemCtrl2Range(PhysicalAddr)).Valid)
    {
        TODO("Handle reading memctrl2: %08x\n", LogicalAddr);
    }
    else if ((Translation = InCacheCtrlRange(PhysicalAddr)).Valid)
    {
        TODO("Handle reading cache ctrl: %08x\n", LogicalAddr);
    }
    else if ((Translation = InInterruptCtrlRange(PhysicalAddr)).Valid)
    {
        /* nop */
        RegionName = "interrupt ctrl";
    }
    else if ((Translation = InDMARange(PhysicalAddr)).Valid)
    {
        RegionName = "DMA";
        Data = DMA_Read32(&Ps1->Dma, Translation.Offset);
        return Data;
    }
    else if ((Translation = InGPURange(PhysicalAddr)).Valid)
    {
        /* nop */
        RegionName = "GPU";
        if (Translation.Offset == 4) /* GPU STAT */
        {
            /* hack to prevent bios from spinning */
            /* set bit 28 (Ready for DMA Block),
             *     bit 27 (Ready to send VRAM to CPU),
             *     bit 26 (Ready for Cmd Word) */
            Data = 0x1C000000; 
        }
    }
    else if ((Translation = InTimerRange(PhysicalAddr)).Valid)
    {
        RegionName = "timer";
    }
    else
    {
        TODO("read32: [%08x] -> %08x\n", LogicalAddr, Data);
    }
    LOG("%s: [%08x] -> %08x\n", RegionName, LogicalAddr, Data);
    return Data;
}

u16 PS1_Read16(PS1 *Ps1, u32 LogicalAddr)
{
    u32 Data = 0;
    const char *RegionName = "Unknown";
    if (LogicalAddr & 1)
    {
        TODO("read16 unaligned: %08x\n", LogicalAddr);
    }

    u32 PhysicalAddr = PS1_GetPhysicalAddr(LogicalAddr);
    TranslatedAddr Translation = InRamRange(PhysicalAddr);
    if (Translation.Valid)
    {
        RegionName = "ram";
        u32 Offset = Translation.Offset;
        ASSERT(Offset + 2 <= PS1_RAM_SIZE && Offset + 2 > Offset);

        Data = Ps1->Ram[Offset + 0];
        Data |= (u16)Ps1->Ram[Offset + 1] << 8;
        return Data;
    }
    else if ((Translation = InInterruptCtrlRange(PhysicalAddr)).Valid)
    {
        /* nop */
        RegionName = "interrupt ctrl";
    }
    else if ((Translation = InSPURange(PhysicalAddr)).Valid)
    {
        RegionName = "SPU";
        return Data; /* too much logging from spu */
    }
    else
    {
        TODO("read16: [%08x]\n", LogicalAddr);
    }

    LOG("%s: [%08x] -> %04x\n", RegionName, LogicalAddr, Data);
    return Data;
}

u8 PS1_Read8(PS1 *Ps1, u32 LogicalAddr)
{
    const char *RegionName = "Unknown";
    u8 Data = 0;
    u32 PhysicalAddr = PS1_GetPhysicalAddr(LogicalAddr);
    TranslatedAddr Translation = InBiosRange(PhysicalAddr);
    if (Translation.Valid)
    {
        RegionName = "bios";
        Data = Ps1->Bios[Translation.Offset];
        return Data; /*  no log for bios (too many reads) */
    }
    else if ((Translation = InRamRange(PhysicalAddr)).Valid)
    {
        RegionName = "ram";
        Data = Ps1->Ram[Translation.Offset];
        return Data; /*  too much ram reads */
    }
    else if ((Translation = InExpansion1Range(PhysicalAddr)).Valid)
    {
        RegionName = "expansion 1";
        Data = 0xFF;
    }
    else
    {
        TODO("Ps1 read8: [%08x]", LogicalAddr);
    }
    LOG("%s: [%08x] -> %02x\n", RegionName, LogicalAddr, Data);
    return Data;
}


void PS1_Write32(PS1 *Ps1, u32 LogicalAddr, u32 Data)
{
    CPU *Cpu = &Ps1->Cpu;
    if (Cpu->SR & (1 << 16)) /* isolate cache bit, ignore all writes */
    {
        return;
    }

    if (LogicalAddr & 3)
    {
        TODO("unaligned write32: [%08x] <- %08x", LogicalAddr, Data);
    }

    const char *RegionName = "Unknown";
    u32 PhysicalAddr = PS1_GetPhysicalAddr(LogicalAddr);
    TranslatedAddr Translation = InRamRange(PhysicalAddr);
    if (Translation.Valid)
    {
        RegionName = "ram";
        u32 Offset = Translation.Offset;
        ASSERT(Offset + 4 <= PS1_RAM_SIZE && Offset < Offset + 4);

        PS1_Ram_Write32(Ps1, Offset, Data);
        return; /*  ram log is unnecessary since there is going to be a lot of ram write */
    }
    else if ((Translation = InMemCtrl1Range(PhysicalAddr)).Valid)
    {
        RegionName = "mem ctrl";
        switch (Translation.Offset)
        {
        case 0: /*  Expansion 1 base */
        {
            ASSERT(Data == 0x1F000000 && "Invalid expansion 1 base addr");
        } break; 
        case 4: /*  Expansion 2 base */
        {
            ASSERT(Data == 0x1F802000 && "Invalid expansion 2 base addr");
        } break;
        }
    }
    else if ((Translation = InMemCtrl2Range(PhysicalAddr)).Valid)
    {
        RegionName = "mem ctrl 2";
        if (Data != 0x00000B88)
        {
            LOG("Unexpected RAM_SIZE value: %08x (expected %08x)\n", Data, 0x00000B88);
            ASSERT(Data == 0x00000B88);
        }
    }
    else if ((Translation = InCacheCtrlRange(PhysicalAddr)).Valid)
    {
        /* nop */
        RegionName = "cache ctrl";
    }
    else if ((Translation = InInterruptCtrlRange(PhysicalAddr)).Valid)
    {
        /* nop */
        RegionName = "interrupt ctrl";
    }
    else if ((Translation = InTimerRange(PhysicalAddr)).Valid)
    {
        /* nop */
        RegionName = "timer";
    }
    else if ((Translation = InDMARange(PhysicalAddr)).Valid)
    {
        RegionName = "DMA";
        LOG("%s: [%08x] <- %08x\n", RegionName, LogicalAddr, Data);
        DMA_Write32(&Ps1->Dma, Translation.Offset, Data);
        return;
    }
    else if ((Translation = InGPURange(PhysicalAddr)).Valid)
    {
        /* nop */
        RegionName = "GPU";
    }
    else
    {
        TODO("Ps1 write32: [%08x] <- %08x", LogicalAddr, Data);
    }
    LOG("%s: [%08x] <- %08x\n", RegionName, LogicalAddr, Data);
}

void PS1_Write16(PS1 *Ps1, u32 LogicalAddr, u16 Data)
{
    if (LogicalAddr & 1)
    {
        TODO("Unaligned write16: [%08x] <- %04x", LogicalAddr, Data);
    }

    const char *RegionName = "Unknown";
    u32 PhysicalAddr = PS1_GetPhysicalAddr(LogicalAddr);
    TranslatedAddr Translation = InRamRange(PhysicalAddr);
    if (Translation.Valid)
    {
        RegionName = "ram";
        u32 Offset = Translation.Offset;
        ASSERT(Offset + 2 > Offset && Offset + 2 <= PS1_RAM_SIZE);

        Ps1->Ram[Offset + 0] = Data;
        Ps1->Ram[Offset + 1] = Data >> 8;
        return;
    }
    else if ((Translation = InSPURange(PhysicalAddr)).Valid)
    {
        RegionName = "SPU";
        return; /* too much logging from spu */
    }
    else if ((Translation = InInterruptCtrlRange(PhysicalAddr)).Valid)
    {
        RegionName = "interrupt ctrl";
    }
    else if ((Translation = InTimerRange(PhysicalAddr)).Valid)
    {
        RegionName = "timer";
    }
    else
    {
        TODO("write16: [%08x] <- %04x", LogicalAddr, Data);
    }
    LOG("%s: [%08x] <- %04x\n", RegionName, LogicalAddr, Data);
}

void PS1_Write8(PS1 *Ps1, u32 LogicalAddr, u8 Data)
{
    const char *RegionName = "Unknown";
    u32 PhysicalAddr = PS1_GetPhysicalAddr(LogicalAddr);
    TranslatedAddr Translation = InRamRange(PhysicalAddr);
    if (Translation.Valid)
    {
        RegionName = "ram";
        Ps1->Ram[Translation.Offset] = Data;
        return; /*  too much ram writes */
    }
    else if ((Translation = InExpansion2Range(PhysicalAddr)).Valid)
    {
        RegionName = "expansion 2";
    }
    else
    {
        TODO("write8: [%08x] <- %02x", LogicalAddr, Data);
    }
    LOG("%s: [%08x] <- %02x\n", RegionName, LogicalAddr, Data);
}



int main(int argc, char **argv)
{
    if (argc < 1)
    {
        printf("Missing bios file.\n");
        return 1;
    }

    PS1 Ps1 = { 0 };
    Ps1.Bios = (u8 *)malloc(PS1_BIOS_SIZE + PS1_RAM_SIZE);
    Ps1.Ram = Ps1.Bios + PS1_BIOS_SIZE;
    ASSERT(Ps1.Bios != NULL);


    const char* FileName = argv[1];
    FILE* f = fopen(FileName, "rb");
    ASSERT(f && "failed to read file");
    {
        fseek(f, 0, SEEK_END);
        iSize FileSize = ftell(f);
        fseek(f, 0, SEEK_SET);

        if (FileSize != PS1_BIOS_SIZE)
        {
            printf("Bios must be exactly 512kb.\n");
            return 1;
        }
        fread(Ps1.Bios, 1, PS1_BIOS_SIZE, f);
    }
    fclose(f);

    PS1_Reset(&Ps1);
    while (1)
    {
        CPU_Clock(&Ps1.Cpu);
    }

    /*  were exiting, so the OS is freeing the memory anyway,  */
    /*  and faster than us, so why bother */
    /*  free(Ps1.Bios) */
    return 0;
}

