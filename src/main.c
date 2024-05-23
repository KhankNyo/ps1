
#include "R3000A.h"
#include "Disassembler.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>


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

#define CMD_BUFFER_SIZE 128
#define DISABLE_CMD "-"
#define BREAKPOINT_CMD "B"
#define BREAKPOINT_ON_READ_CMD "R"
#define BREAKPOINT_ON_WRITE_CMD "W"
#define BREAKPOINT_COP_CMD "C"
#define BREAKPOINT_EXECUTE_CMD "E"
#define POKE_CMD "w"
#define DISASSEMBLE_CMD "D"
#define DUMP_MEM_CMD "d"
#define RUN_CYCLES_CMD "c"
#define RUN_CMD "r"
#define QUIT_CMD "q"
#define HELP_CMD "h"
#define STEP_CMD "n"
#define VIEW_STATE_CMD "v"


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

typedef struct Debugger 
{
    R3000A LastCpuState;

    char LastCmd[CMD_BUFFER_SIZE];
    Bool8 SingleStep;
    Bool8 HasReadWatch;
    Bool8 HasWriteWatch;
    Bool8 HasBreakpoint;
    Bool8 HasCycleCounterWatch;
    Bool8 HasRegWatch;
    Bool8 HasMemWatch;
    Bool8 HasCP0Watch;
    Bool8 Cp0Changed;
    Bool8 HasInstructionWatch;
    u32 LastReadAddr, ReadWatch;
    u32 LastWriteAddr, WriteWatch;
    u32 BreakpointAddr;
    u32 CyclesUntilSingleStep;
    u32 RegWatchValue, RegWatchIndex;
    u32 InstructionWatch;
} Debugger;

typedef struct PS1 
{
    DMA Dma;
    R3000A Cpu;

    u32 BiosRomSize;
    u32 RamSize;
    u8 *BiosRom;
    u8 *Ram;

    Debugger Dbg;
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
    Bool8 IsolateCache = 0 != (Ps1->Cpu.CP0.Status & (1 << 16));

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
        if (!IsolateCache)
        {
            for (uint i = 0; 
                i < (uint)Size 
                && PhysicalAddr + i < Ps1->RamSize; 
                i++)
            {
                Ps1->Ram[PhysicalAddr + i] = Data >> i*8;
            }
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
    Ps1->Dbg.LastReadAddr = Addr;
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
    Ps1->Dbg.LastWriteAddr = Addr;
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

static void Ps1_Disasm(const PS1 *Ps1, u32 LogicalAddr, u32 InstructionCount)
{
    for (u32 i = 0; i < InstructionCount; i++)
    {
        u32 CurrentAddr = LogicalAddr + i*4;
        MemoryInfo Location = Ps1_Read(Ps1, CurrentAddr, 4, true);
        char Mnemonic[64];
        R3000A_Disasm(
            Location.Data, 
            CurrentAddr, 
            DISASM_IMM16_AS_HEX, 
            Mnemonic, 
            sizeof Mnemonic
        );

        const char *Pointer = "    ";
        if (CurrentAddr == Ps1->Cpu.PC)
            Pointer = " PC> ";
        printf("%s%08x: %08x    %s\n", Pointer, CurrentAddr, Location.Data, Mnemonic);
    }
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






static void DumpState(const PS1 *Ps1)
{
#define DISPLAY_REGISTER(reg, fmtstr, ...) \
    do {\
        if (Mips->reg == Ps1->Dbg.LastCpuState.reg)\
            printf(" "fmtstr" ", __VA_ARGS__);\
        else printf("["fmtstr"]", __VA_ARGS__);\
    } while (0)

    const R3000A *Mips = &Ps1->Cpu;
    int RegisterCount = STATIC_ARRAY_SIZE(Mips->R);
    int RegisterBoxPerLine = 4;
    int y = 0;

    /* dump CP0 regs */
    puts("\n========== CP0 Registers ==========");
    DISPLAY_REGISTER(CP0.EPC,       "EPC     =%08x", Mips->CP0.EPC);
    DISPLAY_REGISTER(CP0.Cause,     "Cause   =%08x", Mips->CP0.Cause);
    DISPLAY_REGISTER(CP0.Status,    "Status  =%08x", Mips->CP0.Status);
    DISPLAY_REGISTER(CP0.BadVAddr,  "BadVAddr=%08x", Mips->CP0.BadVAddr);
    printf("\n");

    DISPLAY_REGISTER(CP0.BDA,       "BDA     =%08x", Mips->CP0.BDA);
    DISPLAY_REGISTER(CP0.BPC,       "BPC     =%08x", Mips->CP0.BPC);
    DISPLAY_REGISTER(CP0.BDAM,      "BDAM    =%08x", Mips->CP0.BDAM);
    DISPLAY_REGISTER(CP0.BPCM,      "BPCM    =%08x", Mips->CP0.BPCM);
    printf("\n");

    DISPLAY_REGISTER(CP0.DCIC,      "DCIC    =%08x", Mips->CP0.DCIC);
    DISPLAY_REGISTER(CP0.PrID,      "PrID    =%08x", Mips->CP0.PrID);
    DISPLAY_REGISTER(CP0.JumpDest,  "JumpDest=%08x", Mips->CP0.JumpDest);

    /* dump CPU regs */
    puts("\n========== CPU Registers ==========");
    for (y = 1; y < 1 + RegisterCount / RegisterBoxPerLine; y++)
    {
        for (int x = 0; x < RegisterBoxPerLine; x++)
        {
            int RegisterIndex = (y - 1)*RegisterBoxPerLine + x;
            DISPLAY_REGISTER(
                R[RegisterIndex], 
                "R%02d=%08x", 
                RegisterIndex, 
                Mips->R[RegisterIndex]
            );
        }
        printf("\n");
    }
    printf("PC=%08x\n", Mips->PC);
#undef DISPLAY_REGISTER
}

static Bool8 ParseUint(const char *Str, const char **Next, u32 *Out)
{
    u32 Value = 0;
    while (' ' == *Str || '\t' == *Str || '\n' == *Str || '\r' == *Str)
    {
        Str++;
    }

    if (!IN_RANGE('0', *Str, '9'))
    {
        return false;
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
    *Out = Value;
    return true;
}

static void AsciiDump(u8 *ByteBuffer, iSize ByteBufferSize)
{
    for (uint i = 0; i < ByteBufferSize; i++)
    {
        char Byte = '.';
        if (isprint(ByteBuffer[i]))
            Byte = ByteBuffer[i];
        putc(Byte, stdout);
    }
}

/* returns 0 if should continue polling commands, 
 * returns 1 if there was an error 
 * returns 2 if the user wants to exit */
static int ParseCommand(PS1 *Ps1, const char Cmd[CMD_BUFFER_SIZE], Bool8 EnableCmd)
{
    if (STREQ(Cmd, STEP_CMD))
    {
        return 1;
    }
    else if (STREQ(Cmd, BREAKPOINT_CMD))
    {
        Ps1->Dbg.HasBreakpoint = EnableCmd;
        if (EnableCmd)
        {
            if (ParseUint(Cmd + sizeof(BREAKPOINT_CMD) - 1, NULL, &Ps1->Dbg.BreakpointAddr))
                printf("Enabled breakpoint at 0x%08x\n", Ps1->Dbg.BreakpointAddr);
            else goto ExpectedAddr;
        }
        else
        {
            printf("Disabled breakpoint\n");
        }
    }
    else if (STREQ(Cmd, BREAKPOINT_ON_READ_CMD))
    {
        Ps1->Dbg.HasReadWatch = EnableCmd;
        if (EnableCmd)
        {
            if (ParseUint(Cmd + sizeof(BREAKPOINT_ON_READ_CMD) - 1, NULL, &Ps1->Dbg.ReadWatch))
                printf("Enabled breakpoint on read at 0x%08x\n", Ps1->Dbg.ReadWatch);
            else goto ExpectedAddr;
        }
        else
        {
            printf("Disabled breakpoint on read\n");
        }
    }
    else if (STREQ(Cmd, BREAKPOINT_ON_WRITE_CMD))
    {
        Ps1->Dbg.HasWriteWatch = EnableCmd;
        if (EnableCmd)
        {
            if (ParseUint(Cmd + sizeof(BREAKPOINT_ON_WRITE_CMD) - 1, NULL, &Ps1->Dbg.WriteWatch))
                printf("Enabled breakpoint on write at 0x%08x\n", Ps1->Dbg.WriteWatch);
            else goto ExpectedAddr;
        }
        else
        {
            printf("Disabled breakpoint on write\n");
        }
    }
    else if (STREQ(Cmd, BREAKPOINT_COP_CMD))
    {
        u32 CoprocessorNumber;
        if (!ParseUint(Cmd + sizeof(BREAKPOINT_COP_CMD), NULL, &CoprocessorNumber) 
        || (CoprocessorNumber != 0 && CoprocessorNumber != 2))
        {
            printf("Expected coprocessor number (0 or 2).\n");
            goto Error;
        }

        if (CoprocessorNumber == 0)
        {
            Ps1->Dbg.HasCP0Watch = EnableCmd;
            const char *EnableStr = "Disabled";
            if (EnableCmd)
                EnableStr = "Enabled";

            printf("%s breakpoint on CP0 access.\n", EnableStr);
        }
        else
        {
            ASSERT(false && "COP2 is unimplemented");
        }
    }
    else if (STREQ(Cmd, BREAKPOINT_EXECUTE_CMD))
    {
        u32 Instruction;
        if (!ParseUint(Cmd + sizeof(BREAKPOINT_EXECUTE_CMD), NULL, &Instruction))
        {
            printf("Expexcted instruction code (in hex).\n");
            goto Error;
        }

        Ps1->Dbg.HasInstructionWatch = EnableCmd;
        if (EnableCmd)
        {
            Ps1->Dbg.InstructionWatch = Instruction;
            printf("Enabled instruction watch.\n");
        }
        else
        {
            printf("Disabled Instruction watch.\n");
        }
    }
    else if (STREQ(Cmd, POKE_CMD))
    {
        const char *NextArg;
        u32 Addr;
        if (!ParseUint(Cmd + sizeof(POKE_CMD) - 1, &NextArg, &Addr))
            goto ExpectedAddr;
        u32 Value;
        if (!ParseUint(NextArg, NULL, &Value))
        {
            printf("Expected a number.\n");
            goto Error;
        }

        MemoryInfo Location = Ps1_Read(Ps1, Addr, 4, true);
        Ps1_Write(Ps1, Addr, Value, 4, true);

        printf("%08x: %08x -> %08x (%s)\n", Addr, Location.Data, Value, Location.Name);
    }
    else if (STREQ(Cmd, DISASSEMBLE_CMD))
    {
        const char *NextArg;

        u32 Addr;
        if (STREQ(Cmd + sizeof(DISASSEMBLE_CMD), "PC")) /* Disasm PC <count> */
        {
            /* use PC as <addr> */
            Addr = Ps1->Cpu.PC;
            NextArg = Cmd + sizeof(DISASSEMBLE_CMD" PC");
        }
        else /* Disasm <addr> <count> */
        {
            /* parses <addr> */
            if (!ParseUint(Cmd + sizeof(DISASSEMBLE_CMD) - 1, &NextArg, &Addr))
                goto ExpectedAddr;
        }

        /* parses <count> */
        uint InstructionCount;
        if (!ParseUint(NextArg, NULL, &InstructionCount))
        {
            printf("Expected instruction count.\n");
            goto Error;
        }

        /* disassembles the instructions */
        Ps1_Disasm(Ps1, Addr, InstructionCount);
    }
    else if (STREQ(Cmd, DUMP_MEM_CMD))
    {
        /* parses cmd: dump <addr> <byte_count> */
        const char *NextArg;
        u32 Addr;
        u32 ByteCount;
        if (!ParseUint(Cmd + sizeof(DUMP_MEM_CMD), &NextArg, &Addr))
            goto ExpectedAddr;
        if (!ParseUint(NextArg, NULL, &ByteCount))
        {
            printf("Expected byte count.\n");
            goto Error;
        }

        /* 'hexdump -C' style */
        u32 BytesPerLine = 16;
        u32 i = 0;
        u8 ByteBuffer[256];
        u8 ByteBufferSize = 0;
        for (; i < ByteCount; i++)
        {
            MemoryInfo Mem = Ps1_Read(Ps1, Addr + i, DATA_BYTE, true);
            if (i % BytesPerLine == 0)
            {
                if (i != 0)
                {
                    printf("  |");
                    AsciiDump(ByteBuffer, ByteBufferSize);
                    printf("|");
                    ByteBufferSize = 0;
                }
                /* addr */
                printf("\n%08x: ", (u32)(Addr + i));
            }
            ByteBuffer[ByteBufferSize++] = Mem.Data;
            printf(" %02x", Mem.Data);
        }
        uint Leftover = BytesPerLine - (i % BytesPerLine);
        if (Leftover == BytesPerLine)
            Leftover = 0;

        if (Leftover)
        {
            for (uint i = 0; i < Leftover; i++)
                printf("   ");
        }
        printf("  |");
        AsciiDump(ByteBuffer, ByteBufferSize);
        if (Leftover)
        {
            for (uint i = 0; i < Leftover; i++)
                printf(" ");
        }
        printf("|\n");
    }
    else if (STREQ(Cmd, VIEW_STATE_CMD))
    {
        DumpState(Ps1);
    }
    else if (STREQ(Cmd, HELP_CMD))
    {
        printf(
            "Commands list:\n"
            "\t"STEP_CMD                ":          Advance 1 instruction\n"
            "\t"BREAKPOINT_CMD          " addr:     Sets a breakpoint at addr\n"
            "\t"BREAKPOINT_ON_READ_CMD  " addr:     Halts the emulator when addr was read\n"
            "\t"BREAKPOINT_ON_WRITE_CMD " addr:     Halts the emulator when addr was written to\n"
            "\t"BREAKPOINT_COP_CMD      " n:        Halts the emulator when coprocessor n (0 or 2) was written to or read from\n"
            "\t"BREAKPOINT_EXECUTE_CMD  " n:        Halts the emulator when instruction n (0..0xFFFF_FFFF) is being executed\n"
            "\t"POKE_CMD                " addr n:   Writes the value n to addr\n"
            "\t"DISASSEMBLE_CMD         " addr:     Disassembles instructions at addr\n"
            "\t"DUMP_MEM_CMD            " addr n:   Dumps n bytes at addr\n"
            "\t"RUN_CYCLES_CMD          " n:        Runs the emulator for n instructions\n"
            "\t"RUN_CMD                 ":          Runs the emulator until a breakpoint is hit\n"
            "\t"QUIT_CMD                ":          Exits the emulator\n"
            "\t"HELP_CMD                ":          Shows this message\n"
            "\t"VIEW_STATE_CMD          ":          Shows CPU and CP0 state\n"
            "addr range: 0..0xFFFF_FFFF (can be an unsigned decimal)\n"
            "n    range: 0..0xFFFF_FFFF (unless specified otherwise)\n"
            "Prefix breakpoint commands with '"DISABLE_CMD"' to disable them, \n"
            "\tex: '"DISABLE_CMD" "BREAKPOINT_CMD"' disables breakpoint\n"
        );
    }
    else if (STREQ(Cmd, QUIT_CMD))
    {
        printf("Quitting\n");
        return 2;
    }
    else if (STREQ(Cmd, RUN_CYCLES_CMD))
    {
        Ps1->Dbg.HasCycleCounterWatch = true;
        Ps1->Dbg.CyclesUntilSingleStep = atoi(Cmd + sizeof(RUN_CYCLES_CMD)); /* space is required */
        return 1;
    }
    else if (STREQ(Cmd, RUN_CMD))
    {
        Ps1->Dbg.SingleStep = false;
        Ps1->Dbg.LastReadAddr = -1;
        Ps1->Dbg.LastWriteAddr = -1;
        return 1;
    }

    /* continue parsing */
    return 0;

ExpectedAddr:
    printf("Expected an address.\n");
Error:
    return 0;
}

static Bool8 GetCmdLine(PS1 *Ps1)
{
    do {
        char Input[CMD_BUFFER_SIZE];
        char *Cmd = Input;
        printf(">> ");
        fgets(Cmd, sizeof Input, stdin);
        if (Input[0] != '\n' && Input[0] != '\0')
            memcpy(Ps1->Dbg.LastCmd, Input, CMD_BUFFER_SIZE);
        else memcpy(Input, Ps1->Dbg.LastCmd, CMD_BUFFER_SIZE);

        Bool8 EnableCmd = !STREQ(Cmd, DISABLE_CMD);
        if (!EnableCmd)
            Cmd += sizeof(DISABLE_CMD); /* space counts, only 1 */

        int ShouldQuitOrExit = ParseCommand(Ps1, Cmd, EnableCmd);
        if (!ShouldQuitOrExit)
            continue;
        else if (1 == ShouldQuitOrExit)
            return true;
        else return false;
    } while (1);
}

static void Ps1_ManageWatchdogs(PS1 *Ps1)
{
    Ps1->Dbg.Cp0Changed = 0 != memcmp(&Ps1->Cpu.CP0, &Ps1->Dbg.LastCpuState.CP0, sizeof(Ps1->Cpu.CP0));
    Ps1->Dbg.LastCpuState = Ps1->Cpu;

    Bool8 ShouldChangeSingleStep = 
        (Ps1->Dbg.HasBreakpoint && Ps1->Dbg.BreakpointAddr == Ps1->Cpu.PC)
        || (Ps1->Dbg.HasReadWatch && Ps1->Dbg.LastReadAddr == Ps1->Dbg.ReadWatch)
        || (Ps1->Dbg.HasWriteWatch && Ps1->Dbg.LastWriteAddr == Ps1->Dbg.WriteWatch) 
        || (Ps1->Dbg.HasRegWatch && Ps1->Dbg.RegWatchValue != Ps1->Cpu.R[Ps1->Dbg.RegWatchIndex])
        || (Ps1->Dbg.HasCP0Watch && Ps1->Dbg.Cp0Changed)
        || (Ps1->Dbg.HasInstructionWatch && Ps1->Cpu.Instruction[Ps1->Cpu.PipeStage] == Ps1->Dbg.InstructionWatch);
    if (ShouldChangeSingleStep)
        Ps1->Dbg.SingleStep = !Ps1->Dbg.SingleStep;

    if (Ps1->Dbg.HasCycleCounterWatch)
    {
        if (Ps1->Dbg.CyclesUntilSingleStep == 0)
        {
            Ps1->Dbg.SingleStep = true;
            Ps1->Dbg.HasCycleCounterWatch = false;
        }
        else
        {
            Ps1->Dbg.CyclesUntilSingleStep--;
        }
    }

    if (Ps1->Dbg.HasMemWatch)
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
    Ps1.Dbg.SingleStep = true;
    printf("Type '"HELP_CMD"' to see a list of commands\n\n");
    while (1)
    {
        Ps1_ManageWatchdogs(&Ps1);
        if (Ps1.Dbg.SingleStep)
        {
            Ps1_Disasm(&Ps1, Ps1.Cpu.PC, 1);
            if (!GetCmdLine(&Ps1))
                break;
        }

        R3000A_StepClock(&Ps1.Cpu);
    }
    Ps1_Destroy(&Ps1);
    return 0;

CloseRom:
    fclose(RomFile);
    return 1;
}

