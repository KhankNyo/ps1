#include <stdio.h>
#include <string.h> /* memset, memcpy */

#include "Common.h"
#include "CPU.h"
#include "Ps1.h"

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

        Data = Ps1->Ram[Offset + 0];
        Data |= (u32)Ps1->Ram[Offset + 1] << 8;
        Data |= (u32)Ps1->Ram[Offset + 2] << 16;
        Data |= (u32)Ps1->Ram[Offset + 3] << 24;
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
        /* nop */
        RegionName = "DMA";
    }
    else if ((Translation = InGPURange(PhysicalAddr)).Valid)
    {
        /* nop */
        RegionName = "GPU";
        if (Translation.Offset == 4) /* GPU STAT */
        {
            Data = 0x10000000;
        }
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

        Ps1->Ram[Offset + 0] = Data >> 0;
        Ps1->Ram[Offset + 1] = Data >> 8;
        Ps1->Ram[Offset + 2] = Data >> 16;
        Ps1->Ram[Offset + 3] = Data >> 24;
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
        /* nop */
        RegionName = "DMA";
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

    CPU_Reset(&Ps1.Cpu, &Ps1);
    while (1)
    {
        CPU_Clock(&Ps1.Cpu);
    }

    /*  were exiting, so the OS is freeing the memory anyway,  */
    /*  and faster than us, so why bother */
    /*  free(Ps1.Bios) */
    return 0;
}

