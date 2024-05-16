
#include "R3000A.h"
#include "Disassembler.h"

#include <stdio.h>
#include <string.h>


#define DMA_CHANNEL_COUNT 7

#define KUSEG 0
#define KSEG0 0x80000000
#define KSEG1 0xA0000000 

#define MAIN_RAM(Seg_)      ((Seg_) + 0)
#define EXPANSION_1(Seg_)   ((Seg_) + 0x1F000000)
#define IO_PORTS(Seg_)      ((Seg_) + 0x1F801000)
#define EXPANSION_2(Seg_)   ((Seg_) + 0x1F802000)
#define EXPANSION_3(Seg_)   ((Seg_) + 0x1FA00000)
#define BIOS_ROM(Seg_)      ((Seg_) + 0x1FC00000)

#define ACCESS_RAM(Addr_)               ((u32)(Addr_) < 2*MB)
#define ACCESS_BIOS_ROM(Addr_)          IN_RANGE(BIOS_ROM(0), (u32)(Addr_), BIOS_ROM(0) + 512*KB)
#define ACCESS_DMA_PORTS(Addr_)         IN_RANGE(0x1F801080, (u32)(Addr_), 0x1F8010FF)
#define ACCESS_EXPANSION_1(Addr_)       IN_RANGE(EXPANSION_1(0), (u32)(Addr_), EXPANSION_1(0) + 8192)
#define ACCESS_EXPANSION_2(Addr_)       IN_RANGE(EXPANSION_2(0), (u32)(Addr_), EXPANSION_2(0) + 8192)
#define ACCESS_MEM_CTRL(Addr_)          IN_RANGE(IO_PORTS(0), (u32)(Addr_), IO_PORTS(0) + 0x20)
#define ACCESS_CACHE_CTRL(Addr_)        ((u32)(Addr_) >= 0xFFFE0000)
#define ACCESS_SPU_CTRL(Addr_)          IN_RANGE(0x1F801D80, (u32)(Addr_), 0x1F801DBC)

#define STREQ(s1, strlit) (strncmp(s1, strlit, sizeof strlit - 1) == 0)


typedef struct DMA 
{
    struct {
        u32 Addr;
        union {
            struct {
                u16 BC;
            } Sync0;
            struct {
                u16 BS, 
                    BA;
            } Sync1;
            u32 U32;
        } BlockLen;
        struct {
            unsigned Direction:1;       /* 0 */
            unsigned Decrement:1;       /* 1 */
            unsigned Chopping:1;        /* 2 */
            unsigned SyncType:2;        /* 9..10 */
            unsigned ChopDMAWindow:3;   /* 16..18 */
            unsigned ChopCPUWindow:3;   /* 20..22 */
            unsigned Enable:1;          /* 24 */
            unsigned ManualTrigger:1;   /* 28 */
            unsigned Unknown:2;         /* 29..30 */
        } Ctrl;
    } Channel[DMA_CHANNEL_COUNT];

    struct {
        struct {
            unsigned Priority:3;
            unsigned Enable:1;
        } Channel[DMA_CHANNEL_COUNT];
    } DPCR;

    struct{
        unsigned Unknown:6;     /* 0..5 */
        unsigned ForceIRQ:1;    /* 15 */
        unsigned IRQEnable:7;   /* 16..22 */
        unsigned IRQMaster:1;   /* 23 */
        unsigned IRQStatus:7;   /* 24..30 */
        unsigned IRQActive:1;   /* 31 */
    } DICR;
} DMA;

typedef struct DisassembledInstruction 
{
    char Line[64];
    u32 Instruction;
    u32 Address;
} DisassembledInstruction;

typedef struct PS1 
{
    DMA Dma;
    R3000A Cpu;

    u32 BiosRomSize;
    u32 RamSize;
    u8 *BiosRom;
    u8 *Ram;

    Bool8 SingleStep;
    Bool8 HasReadWatch;
    Bool8 HasWriteWatch;
    Bool8 HasBreakpoint;
    Bool8 HasCycleCounterWatch;
    Bool8 HasRegWatch;
    Bool8 HasMemWatch;
    u32 LastReadAddr, ReadWatch;
    u32 LastWriteAddr, WriteWatch;
    u32 BreakpointAddr;
    u32 CyclesUntilSingleStep;
    u32 RegWatchValue, RegWatchIndex;
} PS1;

typedef struct MemoryInfo 
{
    const char *Name;
    u32 Data;
} MemoryInfo;

static const char *sBiosRomLiteral = "Bios Rom";
static const char *sRamLiteral = "Ram";
static const char *sDMAPortLiteral = "DMA Ports";
static const char *sExpansion1Literal = "Expansion 1";
static const char *sExpansion2Literal = "Expansion 2";
static const char *sSPUCtrlLiteral = "SPU Ctrl";
static const char *sUnknownLiteral = "Unknown";
static const char *sCacheCtrlLiteral = "Cache Ctrl";
static const char *sMemCtrlLiteral = "Mem Ctrl";
static const char *sRamSizeLiteral = "RAM_SIZE";


static u32 Ps1_TranslateAddr(const PS1 *Ps1, u32 Addr)
{
    u32 PhysicalAddr = Addr;
    if (IN_RANGE(KSEG0, Addr, KSEG1 - 1)) /* kseg 0 */
    {
        PhysicalAddr -= KSEG0;
    }
    else if (IN_RANGE(KSEG1, Addr, 0xFFFE0000 - 1)) /* kseg 1 */
    {
        PhysicalAddr -= KSEG1;
    }
    return PhysicalAddr;
}



static u32 DMA_Read(const DMA *Dma, u32 Addr)
{
    u32 Data = 0;
    /* TODO: unaligned read */
    switch (Addr & ~0x7)
    {
    case 0x1F8010F0: /* DPCR */
    {
        for (uint i = 0; i < DMA_CHANNEL_COUNT; i++)
        {
            Data |= (u32)Dma->DPCR.Channel[i].Priority << i*4;
            Data |= (u32)Dma->DPCR.Channel[i].Enable << (i*4 + 3);
        }
    } break;
    case 0x1F8010F4: /* DICR */
    {
        Data = 
            Dma->DICR.Unknown          << 0
            | (u32)Dma->DICR.ForceIRQ  << 15
            | (u32)Dma->DICR.IRQEnable << 16 
            | (u32)Dma->DICR.IRQMaster << 23 
            | (u32)Dma->DICR.IRQStatus << 24 
            | (u32)Dma->DICR.IRQActive << 31;
    } break;
    case 0x1F8010F8:
    case 0x1F8010FC:
    {
        /* unknown channels (prob a nop) */
    } break;
    default: /* dma channels */
    {
        u32 ChannelIndex = (Addr & 0x70) >> 4;
        u32 RegIndex = (Addr & 0x0F) / sizeof(u32);
        switch (RegIndex)
        {
        case 0: /* addr */
        {
            Data = Dma->Channel[ChannelIndex].Addr;
        } break;
        case 1: /* len */
        {
            Data = Dma->Channel[ChannelIndex].BlockLen.U32;
        } break;
        case 2: /* ctrl */
        {
            Data = 
                (u32)Dma->Channel[ChannelIndex].Ctrl.Direction          << 0
                | (u32)Dma->Channel[ChannelIndex].Ctrl.Decrement        << 1
                | (u32)Dma->Channel[ChannelIndex].Ctrl.Chopping         << 2
                | (u32)Dma->Channel[ChannelIndex].Ctrl.SyncType         << 9
                | (u32)Dma->Channel[ChannelIndex].Ctrl.ChopDMAWindow    << 16
                | (u32)Dma->Channel[ChannelIndex].Ctrl.ChopCPUWindow    << 20
                | (u32)Dma->Channel[ChannelIndex].Ctrl.Enable           << 24
                | (u32)Dma->Channel[ChannelIndex].Ctrl.ManualTrigger    << 28
                | (u32)Dma->Channel[ChannelIndex].Ctrl.Unknown          << 29
                ;
        } break;
        default:
        {
            printf("Reading Index: %d, addr: 0x%08x\n", RegIndex, Addr);
            ASSERT(false && "Unreachable");
        } break;
        }
    } break;
    }
    return Data;
}

static void DMA_Write(DMA *Dma, u32 Addr, u32 Data)
{
    /* TODO: unaligned write */
    switch (Addr & ~0x7)
    {
    case 0x1F8010F0: /* DPCR */
    {
        for (uint i = 0; i < DMA_CHANNEL_COUNT; i++)
        {
            Dma->DPCR.Channel[i].Enable     = Data >> (3 + i*4);
            Dma->DPCR.Channel[i].Priority   = Data >> (0 + i*4);
        }
    } break;
    case 0x1F8010F4: /* DICR */
    {
        Dma->DICR.Unknown   = Data >> 0;
        Dma->DICR.ForceIRQ  = Data >> 15;
        Dma->DICR.IRQEnable = Data >> 16;
        Dma->DICR.IRQMaster = Data >> 23;
        Dma->DICR.IRQStatus = Data >> 24;
    } break;
    case 0x1F8010F8:
    case 0x1F8010FC:
    {
        /* unknown channels (prob a nop) */
    } break;
    default: /* dma channels */
    {
        u32 ChannelIndex = (Addr & 0x70) >> 4;
        u32 RegIndex = (Addr & 0x0F) / sizeof(u32);
        switch (RegIndex)
        {
        case 0: /* addr */
        {
            Dma->Channel[ChannelIndex].Addr = Data;
        } break;
        case 1: /* len */
        {
            Dma->Channel[ChannelIndex].BlockLen.U32 = Data;
        } break;
        case 2: /* ctrl */
        {
            Dma->Channel[ChannelIndex].Ctrl.Direction        = Data >> 0;
            Dma->Channel[ChannelIndex].Ctrl.Decrement        = Data >> 1;
            Dma->Channel[ChannelIndex].Ctrl.Chopping         = Data >> 2;
            Dma->Channel[ChannelIndex].Ctrl.SyncType         = Data >> 9;
            Dma->Channel[ChannelIndex].Ctrl.ChopDMAWindow    = Data >> 16;
            Dma->Channel[ChannelIndex].Ctrl.ChopCPUWindow    = Data >> 20;
            Dma->Channel[ChannelIndex].Ctrl.Enable           = Data >> 24;
            Dma->Channel[ChannelIndex].Ctrl.ManualTrigger    = Data >> 28;
            Dma->Channel[ChannelIndex].Ctrl.Unknown          = Data >> 29;
        } break;
        default:
        {
            printf("Writing Index: %d, addr: 0x%08x\n", RegIndex, Addr);
            ASSERT(false && "Unreachable");
        } break;
        }
    } break;
    }
}


static MemoryInfo Ps1_Read(const PS1 *Ps1, u32 Addr, R3000A_DataSize Size, Bool8 DebugRead)
{
    u32 PhysicalAddr = Ps1_TranslateAddr(Ps1, Addr);
    MemoryInfo Info = { 0 };
    if (ACCESS_BIOS_ROM(PhysicalAddr))
    {
        PhysicalAddr -= BIOS_ROM(0);
        for (uint i = 0; i < (uint)Size && PhysicalAddr + i < Ps1->BiosRomSize; i++)
        {
            Info.Data |= (u32)Ps1->BiosRom[PhysicalAddr + i] << i*8;
        }
        Info.Name = sBiosRomLiteral;
    }
    else if (ACCESS_RAM(PhysicalAddr))
    {
        PhysicalAddr -= MAIN_RAM(0);
        for (uint i = 0; i < (uint)Size && PhysicalAddr + i < Ps1->RamSize; i++)
        {
            Info.Data |= (u32)Ps1->Ram[PhysicalAddr + i] << i*8;
        }
        Info.Name = sRamLiteral;
    }
    else if (ACCESS_DMA_PORTS(PhysicalAddr))
    {
        Info.Data = DMA_Read(&Ps1->Dma, PhysicalAddr);
        Info.Name = sDMAPortLiteral;
    }
    else if (ACCESS_EXPANSION_1(PhysicalAddr))
    {
        Info.Data = 0xFF;
        Info.Name = sExpansion1Literal;
    }
    else if (ACCESS_EXPANSION_2(PhysicalAddr))
    {
        Info.Data = 0xFF;
        Info.Name = sExpansion2Literal;
    }
    else if (ACCESS_MEM_CTRL(PhysicalAddr))
    {
        Info.Name = sMemCtrlLiteral;
    }
    else if (PhysicalAddr == 0x1F801060) /* RAM_SIZE */
    {
        Info.Name = sRamSizeLiteral;
    }
    else if (ACCESS_SPU_CTRL(PhysicalAddr))
    {
        Info.Name = sSPUCtrlLiteral;
    }
    else if (ACCESS_CACHE_CTRL(PhysicalAddr))
    {
        Info.Name = sCacheCtrlLiteral;
    }
    else 
    {
        Info.Name = sUnknownLiteral;
    }
    return Info;
}

static const char *Ps1_Write(PS1 *Ps1, u32 Addr, u32 Data, R3000A_DataSize Size, Bool8 DebugWrite)
{
    u32 PhysicalAddr = Ps1_TranslateAddr(Ps1, Addr);
    if (DebugWrite && ACCESS_BIOS_ROM(PhysicalAddr))
    {
        for (uint i = 0; i < (uint)Size && PhysicalAddr + i < Ps1->BiosRomSize; i++)
        {
            Ps1->BiosRom[PhysicalAddr + i] = Data >> i*8;
        }
        return sBiosRomLiteral;
    }
    if (ACCESS_RAM(PhysicalAddr))
    {
        for (uint i = 0; i < (uint)Size && PhysicalAddr + i < Ps1->RamSize; i++)
        {
            Ps1->Ram[PhysicalAddr + i] = Data >> i*8;
        }
        return sRamLiteral;
    }
    if (ACCESS_DMA_PORTS(PhysicalAddr))
    {
        DMA_Write(&Ps1->Dma, Addr, Data);
        return sDMAPortLiteral;
    }
    if (ACCESS_EXPANSION_1(PhysicalAddr))
    {
        /* nop */
        return sExpansion1Literal;
    }
    if (ACCESS_EXPANSION_2(PhysicalAddr))
    {
        /* nop */
        return sExpansion2Literal;
    }
    if (ACCESS_MEM_CTRL(PhysicalAddr))
    {
        /* nop */
        return sMemCtrlLiteral;
    }
    if (PhysicalAddr == 0x1F801060) /* RAM_SIZE */
    {
        /* nop */
        return sRamSizeLiteral;
    }
    if (ACCESS_CACHE_CTRL(PhysicalAddr))
    {
        /* nop */
        return sCacheCtrlLiteral;
    }
    if (ACCESS_SPU_CTRL(PhysicalAddr))
    {
        /* nop */
        return sSPUCtrlLiteral;
    }
    
    return sUnknownLiteral;
}

static u32 Ps1_ReadFn(void *UserData, u32 Addr, R3000A_DataSize Size)
{
    PS1 *Ps1 = UserData;
    Ps1->LastReadAddr = Addr;
    MemoryInfo Location = Ps1_Read(Ps1, Addr, Size, false);

    if (Location.Name != sBiosRomLiteral 
    && Location.Name != sRamLiteral)
    {
        printf("Reading %d bytes from 0x%08x: %08x (%s)\n", Size, Addr, Location.Data, Location.Name);
    }
    return Location.Data;
}

static void Ps1_WriteFn(void *UserData, u32 Addr, u32 Data, R3000A_DataSize Size)
{
    PS1 *Ps1 = UserData;
    Ps1->LastWriteAddr = Addr;
    const char *LocationName = Ps1_Write(Ps1, Addr, Data, Size, false);
    if (LocationName != sRamLiteral
    && LocationName != sBiosRomLiteral)
    {
        printf("Writing 0x%08x (%d bytes) to 0x%08x (%s)\n", Data, Size, Addr, LocationName);
    }
}

static Bool8 Ps1_VerifyAddr(void *UserData, u32 Addr)
{
    (void)UserData, (void)Addr;
    return true;
}

static void Ps1_Init(PS1 *Ps1)
{
    *Ps1 = (PS1){
        .Cpu = R3000A_Init(Ps1, Ps1_ReadFn, Ps1_WriteFn, Ps1_VerifyAddr, Ps1_VerifyAddr),
        .BiosRomSize = 512 * KB,
        .RamSize = 2 * MB,
    };

    /* default priority, 0x07654321 */
    for (uint i = 0; i < DMA_CHANNEL_COUNT; i++)
    {
        Ps1->Dma.DPCR.Channel[i].Priority = i + 1;
    }

    u8 *Ptr = malloc(Ps1->BiosRomSize + Ps1->RamSize);
    ASSERT(NULL != Ptr);
    Ps1->BiosRom = Ptr;
    Ps1->Ram = Ptr + Ps1->BiosRomSize;
}

static void Ps1_Destroy(PS1 *Ps1)
{
    free(Ps1->BiosRom);
    Ps1->BiosRom = NULL;
    Ps1->Ram = NULL;
}






static void DumpState(const R3000A *Mips)
{
    static R3000A LastState;
    int RegisterCount = STATIC_ARRAY_SIZE(Mips->R);
    int RegisterBoxPerLine = 4;
    int y = 0;

    /* dump regs */
    const char *Display = "========== Registers ==========";
    printf("%s\n", Display);

    for (y = 1; y < 1 + RegisterCount / RegisterBoxPerLine; y++)
    {
        for (int x = 0; x < RegisterBoxPerLine; x++)
        {
            int RegisterIndex = (y - 1)*RegisterBoxPerLine + x;
            if (Mips->R[RegisterIndex] == LastState.R[RegisterIndex])
                printf(" R%02d %08x ", RegisterIndex, Mips->R[RegisterIndex]);
            else printf("[R%02d=%08x]", RegisterIndex, Mips->R[RegisterIndex]);
        }
        printf("\n");
    }

    printf("PC=0x%08x\n", Mips->PC);
}

static u32 ParseUint(const char *Str, const char **Next)
{
    u32 Value = 0;
    while (' ' == *Str || '\t' == *Str || '\n' == *Str || '\r' == *Str)
    {
        Str++;
    }

    if (Str[0] == '0' && Str[1] == 'x') /* base 16, 0x */
    {
        Str += 2;
        while (1)
        {
            char Upper = *Str & ~(1 << 5);
            if (IN_RANGE('0', *Str, '9'))
            {
                Value *= 16;
                Value += *Str - '0';
            }
            else if (IN_RANGE('A', Upper, 'F'))
            {
                Value *= 16;
                Value += Upper - 'A' + 10;
            }
            else if ('_' == *Str) 
            {
                /* nop */
            }
            else break;

            Str++;
        }
    }
    else /* base 10 */
    {
        while (1)
        {
            if (IN_RANGE('0', *Str, '9'))
            {
                Value *= 10;
                Value += *Str - '0';
            }
            else if ('_' == *Str) 
            {
                /* nop */
            }
            else break;
            Str++;
        }
    }

    if (Next)
        *Next = Str;
    return Value;
}

static Bool8 GetCmdLine(PS1 *Ps1, const char *Prompt)
{
    printf("%s, type Help to see a list of commands\n\n", Prompt);
    do {
        char Input[256];
        char *Cmd = Input;
        printf(">> ");
        fgets(Cmd, sizeof Input, stdin);

        Bool8 EnableCmd = !STREQ(Cmd, "Disable");
        if (!EnableCmd)
            Cmd += sizeof("Disable"); /* space counts, only 1 */

        if (STREQ(Cmd, "Breakpoint"))
        {
            Ps1->HasBreakpoint = EnableCmd;
            if (EnableCmd)
            {
                Ps1->BreakpointAddr = ParseUint(Cmd + sizeof("Breakpoint") - 1, NULL);
                printf("Enabled breakpoint at 0x%08x\n", Ps1->BreakpointAddr);
            }
            else
            {
                printf("Disabled breakpoint\n");
            }
        }
        else if (STREQ(Cmd, "BreakOnRead"))
        {
            Ps1->HasReadWatch = EnableCmd;
            if (EnableCmd)
            {
                Ps1->ReadWatch = ParseUint(Cmd + sizeof("BreakOnRead") - 1, NULL);
                printf("Enabled breakpoint on read at 0x%08x\n", Ps1->ReadWatch);
            }
            else
            {
                printf("Disabled breakpoint on read\n");
            }
        }
        else if (STREQ(Cmd, "BreakOnWrite"))
        {
            Ps1->HasWriteWatch = EnableCmd;
            if (EnableCmd)
            {
                Ps1->WriteWatch = ParseUint(Cmd + sizeof("BreakOnWrite") - 1, NULL);
                printf("Enabled breakpoint on write at 0x%08x\n", Ps1->WriteWatch);
            }
            else
            {
                printf("Disabled breakpoint on write\n");
            }
        }
        else if (STREQ(Cmd, "Peek"))
        {
            u32 Addr = ParseUint(Cmd + sizeof("Peek") - 1, NULL);
            MemoryInfo Location = Ps1_Read(Ps1, Addr, 4, true);

            printf("%08x: %08x (%s)\n", Addr, Location.Data, Location.Name);
        }
        else if (STREQ(Cmd, "Poke"))
        {
            const char *NextArg;
            u32 Addr = ParseUint(Cmd + sizeof("Poke") - 1, &NextArg);
            u32 Value = ParseUint(NextArg, NULL);

            MemoryInfo Location = Ps1_Read(Ps1, Addr, 4, true);
            Ps1_Write(Ps1, Addr, Value, 4, true);

            printf("%08x: %08x -> %08x (%s)\n", Addr, Location.Data, Value, Location.Name);
        }
        else if (STREQ(Cmd, "Disasm"))
        {
            const char *NextArg;
            u32 Addr = ParseUint(Cmd + sizeof("Disasm") - 1, &NextArg);
            uint InstructionCount = ParseUint(NextArg, NULL);
            for (u32 i = 0; i < InstructionCount; i++)
            {
                u32 CurrentAddr = Addr + i*4;
                MemoryInfo Location = Ps1_Read(Ps1, CurrentAddr, 4, true);
                char Mnemonic[64];
                R3000A_Disasm(
                    Location.Data, 
                    CurrentAddr, 
                    DISASM_IMM16_AS_HEX, 
                    Mnemonic, 
                    sizeof Mnemonic
                );

                const char *Pointer = "   ";
                if (CurrentAddr == Ps1->Cpu.PC)
                    Pointer = "PC>";
                printf("%s%08x: %08x    %s\n", Pointer, CurrentAddr, Location.Data, Mnemonic);
            }
        }
        else if (STREQ(Cmd, "Help"))
        {
            printf(
                "Commands list:\n"
                "\tBreakpoint 0x123456:   Sets a breakpoint at address 0x123456\n"
                "\tBreakOnRead 0x123456:  Halts the emulator when address 0x123456 was read\n"
                "\tBreakOnWrite 0x123456: Halts the emulator when address 0x123456 was written to\n"
                "\tPeek 0x123456:         Reads a word (4 bytes) at address 0x123456\n"
                "\tPoke 0x123456 420:     Writes 420 to address 0x123456\n"
                "\tDisasm 0x123456:       Disassembles instructions at 0x123456\n"
                "\tRuntil 56:             Runs the emulator for 56 instructions\n"
                "\tRun:                   Runs the emulator\n"
                "\tQuit/q:                Exits the emulator\n"
                "\tHelp:                  Shows this message\n"
                "Prefix breakpoint commands with 'Disable' to disable them\n"
            );
        }
        else if (STREQ(Cmd, "Quit") || Cmd[0] == 'q')
        {
            printf("Quitting\n");
            return false;
        }
        else if (STREQ(Cmd, "Runtil"))
        {
            Ps1->HasCycleCounterWatch = true;
            Ps1->CyclesUntilSingleStep = atoi(Cmd + sizeof("runtil")); /* space is required */
            break;
        }
        else if (STREQ(Cmd, "Run"))
        {
            Ps1->SingleStep = false;
            break;
        }
        else break;
    } while (1);
    return true;
}

static void Ps1_ManageWatchdogs(PS1 *Ps1)
{
    Bool8 ShouldChangeSingleStep = (Ps1->HasBreakpoint && Ps1->BreakpointAddr == Ps1->Cpu.PC)
        || (Ps1->HasReadWatch && Ps1->LastReadAddr == Ps1->ReadWatch)
        || (Ps1->HasWriteWatch && Ps1->LastWriteAddr == Ps1->WriteWatch) 
        || (Ps1->HasRegWatch && Ps1->RegWatchValue != Ps1->Cpu.R[Ps1->RegWatchIndex]);
    Ps1->SingleStep = ((Ps1->SingleStep ^ ShouldChangeSingleStep) & 1);

    if (Ps1->HasCycleCounterWatch)
    {
        if (Ps1->CyclesUntilSingleStep == 0)
        {
            Ps1->SingleStep = true;
            Ps1->HasCycleCounterWatch = false;
        }
        else
        {
            Ps1->CyclesUntilSingleStep--;
        }
    }

    if (Ps1->HasMemWatch)
    {
        ASSERT(false && "TODO");
    }
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        printf("Usage: %s <PS 1 Rom file>\n", argv[0]);
        return 1;
    }

    PS1 Ps1;
    Ps1_Init(&Ps1);
    const char *FileName = argv[1];
    FILE *RomFile = fopen(FileName, "rb");
    {
        ASSERT(RomFile);

        fseek(RomFile, 0, SEEK_END);
        iSize FileSize = ftell(RomFile);
        fseek(RomFile, 0, SEEK_SET);

        if (FileSize != Ps1.BiosRomSize)
        {
            printf("Error: rom size must be exactly %d KB\n", (int)(Ps1.BiosRomSize)/KB);
            goto CloseRom;
        }
        u32 ReadSize = fread(Ps1.BiosRom, 1, Ps1.BiosRomSize, RomFile);
        if (ReadSize != Ps1.BiosRomSize)
        {
            printf("Error: unable to fully read '%s' (read %d bytes).", FileName, ReadSize);
            goto CloseRom;
        }
    }
    fclose(RomFile);

    Ps1.Cpu = R3000A_Init(
        &Ps1, 
        Ps1_ReadFn, 
        Ps1_WriteFn, 
        Ps1_VerifyAddr, 
        Ps1_VerifyAddr
    );
    Ps1.SingleStep = true;
    do {
        Ps1_ManageWatchdogs(&Ps1);
        if (Ps1.SingleStep)
        {
            DumpState(&Ps1.Cpu);
            if (!GetCmdLine(&Ps1, "Press Enter to cont..."))
                break;
        }

        R3000A_StepClock(&Ps1.Cpu);
    } while (1);
    Ps1_Destroy(&Ps1);
    return 0;

CloseRom:
    fclose(RomFile);
    return 1;
}

