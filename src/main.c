#include <stdio.h>
#include <string.h> /* memset, memcpy */

#include "Common.h"
#include "CPU.h"
#include "Ps1.h"


void GPU_Reset(GPU *Gpu, PS1 *Bus)
{
    *Gpu = (GPU) {
        .Bus = Bus,
        .GP0Mode = GP0_COMMAND,

        .Status = (GPUStat) {
            .DisplayDisable = 1,
            .TextureDisable = 1,
            .VideoMode = 0, /* NTSC */
        },

        .TextureWindowMaskX = 0,
        .TextureWindowMaskY = 0,
        .TextureWindowOffsetX = 0,
        .TextureWindowOffsetY = 0,

        .DrawingAreaLeft = 0,
        .DrawingAreaRight = 0,
        .DrawingAreaTop = 0,
        .DrawingAreaBottom = 0,

        .DisplayHorizontalStart = 0x200,
        .DisplayHorizontalEnd = 0xC00,
        .DisplayLineStart = 0x10,
        .DisplayLineEnd = 0x100,
    };

    /* TODO: clear fifo */
    /* TODO: clear GPU cache */
}

u32 GPU_ReadGPU(GPU *Gpu)
{
    /* TODO: implement this */
    return 0;
}

u32 GPU_ReadStatus(GPU *Gpu)
{
    u32 Value = 0;
    Value |= (u32)Gpu->Status.TexturePageX << 0;
    Value |= (u32)Gpu->Status.TexturePageY << 4;
    Value |= (u32)Gpu->Status.SemiTransparency << 5;
    Value |= (u32)Gpu->Status.TextureDepth << 7;
    Value |= (u32)Gpu->Status.DitherEnable << 9;
    Value |= (u32)Gpu->Status.DrawEnable << 10;
    Value |= (u32)Gpu->Status.SetMaskBitOnDraw << 11;
    Value |= (u32)Gpu->Status.PreserveMaskedPixel << 12;
    Value |= (u32)Gpu->Status.Field << 13;

    /* bit 14: distortion, not supported */
    // Value |= 1 << 14;
    Value |= (u32)Gpu->Status.TextureDisable << 15;
    Value |= (u32)Gpu->Status.HorizontalResolution << 16;
    //Value |= (u32)Gpu->Status.VerticalResolution << 19;
    Value |= (u32)0 << 19; /* TODO: hack: use 240 bit vertical res to work around infinite loop in bios */
    Value |= (u32)Gpu->Status.VideoMode << 20;
    Value |= (u32)Gpu->Status.DisplayRGB24 << 21;
    Value |= (u32)Gpu->Status.InterlaceEnable << 22;
    Value |= (u32)Gpu->Status.DisplayDisable << 23;
    Value |= (u32)Gpu->Status.Interrupt << 24;

    /* TODO: this is a hack: pretend that the GPU is always ready for now. */
    Gpu->Status.ReadyToSend = 1;
    Gpu->Status.ReadyToReceiveCmdWord = 1;
    Gpu->Status.ReadyToReceiveDMABlock = 1;
    Value |= (u32)Gpu->Status.ReadyToReceiveCmdWord << 26; /* Ready to Receive CMD Word */
    Value |= (u32)Gpu->Status.ReadyToSend << 27; /* Ready to send */
    Value |= (u32)Gpu->Status.ReadyToReceiveDMABlock << 28; /* Ready to receive DMA Block */

    Value |= (u32)Gpu->Status.DMADirection << 29;

    /* bit 31 is enabled during odd line in interlaced mode while not being in vblank */
    Value |= 0 << 31;

    /* bit 25 depends on DMA direction (29..30) */
    u32 DMARequest = 0;
    switch (Gpu->Status.DMADirection)
    {
    case 0: /* off, always 0 */
    {
        DMARequest = 0;
    } break;
    case 1: /* fifo, fifo status (1/0 = empty/full) */
    {
        DMARequest = 1; /* TODO: hack: set to always empty for now */
    } break;
    case 2: /* GPU to GP0, copy bit 28 */
    {
        DMARequest = Gpu->Status.ReadyToReceiveDMABlock;
    } break;
    case 3: /* GP0 to CPU, copy bit 27 */
    {
        DMARequest = Gpu->Status.ReadyToSend;
    } break;
    }
    Value |= DMARequest << 25;

    return Value;
}

static void GP0_SetDrawMode(GPU *Gpu);
static void GP0_SetTextureWindow(GPU *Gpu);
static void GP0_SetDrawingTopLeft(GPU *Gpu);
static void GP0_SetDrawingBottomRight(GPU *Gpu);
static void GP0_SetDrawingOffset(GPU *Gpu);
static void GP0_SetMaskBits(GPU *Gpu);
static void GP0_RenderQuadMonoOpaque(GPU *Gpu);
static void GP0_ClearTextureCache(GPU *Gpu);
static void GP0_LoadImage(GPU *Gpu);

static void GP1_SetDisplayMode(GPU *Gpu, u32 Instruction);

void GPU_WriteGP0(GPU *Gpu, u32 Data)
{
    if (0 == Gpu->CommandWordsRemain)
    {
        Gpu->CommandBufferSize = 0;
        Gpu->CommandWordsRemain = 1;
        u8 Command = Data >> 24;
        switch (Command)
        {
        case 0x00: /* nop */ 
        {
            Gpu->CommandBufferFn = NULL;
        } break;
        case 0x01: /* clear texture cache */
        {
            Gpu->CommandBufferFn = GP0_ClearTextureCache;
        } break;
        case 0xE1: /* set drawing mode (status reg and misc) */
        {
            Gpu->CommandBufferFn = GP0_SetDrawMode;
        } break;
        case 0xE2: /* Texture window setting */
        {
            Gpu->CommandBufferFn = GP0_SetTextureWindow;
        } break;
        case 0xE3: /* set drawing area top left */
        {
            Gpu->CommandBufferFn = GP0_SetDrawingTopLeft;
        } break;
        case 0xE4: /* set drawing area bottom right */
        {
            Gpu->CommandBufferFn = GP0_SetDrawingBottomRight;
        } break;
        case 0xE5: /* set drawing offset */
        {
            Gpu->CommandBufferFn = GP0_SetDrawingOffset;
        } break;
        case 0xE6: /* set mask bits */
        {
            Gpu->CommandBufferFn = GP0_SetMaskBits;
        } break;

        case 0x28: /* render quad mono opaque */
        {
            Gpu->CommandBufferFn = GP0_RenderQuadMonoOpaque;
            Gpu->CommandWordsRemain += 4;
        } break;
        case 0xA0: /* load image */
        {
            Gpu->CommandBufferFn = GP0_LoadImage;
            Gpu->CommandWordsRemain += 2;
        } break;
        default:
        {
            TODO("Unhandled GP0 opcode: %08x\n", Data);
        } break;
        }
    }

    switch (Gpu->GP0Mode)
    {
    case GP0_COMMAND:
    {
        ASSERT(Gpu->CommandWordsRemain);
        /* push the data word into a temp buffer, execute when sufficient amount of words have been fetched for a command */
        Gpu->CommandBuffer[Gpu->CommandBufferSize++] = Data;
        Gpu->CommandWordsRemain--;

        if (0 == Gpu->CommandWordsRemain 
        && NULL != Gpu->CommandBufferFn)
        {
            LOG("[GP0 Cmd]: %08x\n", Gpu->CommandBuffer[0]);
            Gpu->CommandBufferFn(Gpu);
            Gpu->CommandBufferFn = NULL;
        }
    } break;
    case GP0_LOAD_IMAGE:
    {
        Gpu->CommandWordsRemain--;
        if (0 == Gpu->CommandWordsRemain) /* done transfering, switch back to command mode */
        {
            Gpu->GP0Mode = GP0_COMMAND;
            Gpu->CommandBufferSize = 0;
        }
    } break;
    }
}

void GPU_WriteGP1(GPU *Gpu, u32 Data)
{
    u8 Command = Data >> 24;
    switch (Command)
    {
    case 0x00: /* soft reset */ 
    {
        /* NOTE: this piece of code is buggy when compiled with tcc, probably a bitfield bug */
        GPU_Reset(Gpu, Gpu->Bus);
        Gpu->Status.InterlaceEnable = 1;
    } break;
    case 0x03: /* display enable */
    {
        Gpu->Status.DisplayDisable = Data & 1;
    } break;
    case 0x04: /* set DMA direction */
    {
        Gpu->Status.DMADirection = Data;
    } break;
    case 0x05: /* set start of display area */
    {
        Gpu->DisplayVRAMStartX = Data & 0x3FE; /* halfword aligned (nowhere in docs??) */
        Gpu->DisplayVRAMStartY = (Data >> 10) & 0x3FF;
    } break;
    case 0x06: /* set horizontal display range */
    {
        Gpu->DisplayHorizontalStart = Data & 0xFFF;
        Gpu->DisplayHorizontalEnd = (Data >> 12) & 0xFFF;
    } break;
    case 0x07: /* set vertical display range */
    {
        Gpu->DisplayLineStart = Data & 0xFFF;
        Gpu->DisplayLineEnd = (Data >> 12) & 0xFFF;
    } break;
    case 0x08: /* set display mode */
    {
        GP1_SetDisplayMode(Gpu, Data);
    } break;
    default:
    {
        TODO("Unhandled GP1 opcode: %08x", Data);
    } break;
    }
}


static void GP0_SetDrawMode(GPU *Gpu)
{
    u32 Instruction = Gpu->CommandBuffer[0];
    Gpu->Status.TexturePageX = Instruction >> 0;
    Gpu->Status.TexturePageY = Instruction >> 4;
    Gpu->Status.SemiTransparency = Instruction >> 5;
    Gpu->Status.TextureDepth = Instruction >> 7;
    Gpu->Status.DitherEnable = Instruction >> 9;
    Gpu->Status.DrawEnable = Instruction >> 10;
    Gpu->Status.TextureDisable = Instruction >> 11;

    Gpu->TexturedRectangleXFlip = Instruction >> 12;
    Gpu->TexturedRectangleYFlip = Instruction >> 13;
}

static void GP0_SetDrawingOffset(GPU *Gpu)
{
    u32 Instruction = Gpu->CommandBuffer[0];
    i16 X = (i16)(Instruction << 5) >> 5; /* bits 0..10 */
    i16 Y = (i16)(Instruction >> 5) >> 5; /* bits 11..21 */
    Gpu->DrawingOffsetX = X;
    Gpu->DrawingOffsetY = Y;
}

static void GP0_SetDrawingTopLeft(GPU *Gpu)
{
    u32 Instruction = Gpu->CommandBuffer[0];
    Gpu->DrawingAreaTop = (Instruction >> 10) & 0x3FF;
    Gpu->DrawingAreaLeft = (Instruction >> 0) & 0x3FF;
}

static void GP0_SetDrawingBottomRight(GPU *Gpu)
{
    u32 Instruction = Gpu->CommandBuffer[0];
    Gpu->DrawingAreaBottom = (Instruction >> 10) & 0x3FF;
    Gpu->DrawingAreaRight = (Instruction >> 0) & 0x3FF;
}

static void GP0_SetTextureWindow(GPU *Gpu)
{
    u32 Instruction = Gpu->CommandBuffer[0];
    Gpu->TextureWindowMaskX = Instruction & 0x1F;
    Gpu->TextureWindowMaskY = (Instruction >> 5) & 0x1F;
    Gpu->TextureWindowOffsetX = (Instruction >> 10) & 0x1F;
    Gpu->TextureWindowOffsetY = (Instruction >> 15) & 0x1F;
}

static void GP0_SetMaskBits(GPU *Gpu)
{
    u32 Instruction = Gpu->CommandBuffer[0];
    Gpu->Status.SetMaskBitOnDraw = Instruction & 1;
    Gpu->Status.PreserveMaskedPixel = Instruction & 2;
}

static void GP0_RenderQuadMonoOpaque(GPU *Gpu)
{
    (void)Gpu;
    LOG("[Renderer]: Quad Mono Opaque: %d words\n", Gpu->CommandBufferSize);
    /* TODO: implement */
}

static void GP0_ClearTextureCache(GPU *Gpu)
{
    (void)Gpu;
    /* TODO: implement */
}

static void GP0_LoadImage(GPU *Gpu)
{
    /* 
     * Command breakdown (3 words)
     * 0: Command
     * 1: dst: YYYYXXXX, x in halfwords 
     * 2: width + height: HHHHWWWW, w in halfwords
     * there will be padding if number of halfword transfered is odd
     */
    u32 SizeParam = Gpu->CommandBuffer[2];

    /* width * height */
    u32 ImageSizeHalf = (SizeParam & 0xFFFF) * (SizeParam >> 16); 

    /* round up to even multiple of halfword, divide by 2 (sizeof(word)/sizeof(halfword)) */
    u32 ImageSizeWord = (ImageSizeHalf + 1) / 2;

    /* set up GP0 port for image transfer mode */
    Gpu->GP0Mode = GP0_LOAD_IMAGE;
    Gpu->CommandWordsRemain = ImageSizeWord;
    /* TODO: implement the copy when VRAM is added (using dst param) */
    LOG("img transfer: %d words\n", ImageSizeWord);

}



static void GP1_SetDisplayMode(GPU *Gpu, u32 Instruction)
{
    u32 HorRes = (Instruction >> 6) & 0x1;
    HorRes |= (Instruction & 3) << 1;
    Gpu->Status.HorizontalResolution = HorRes;
    Gpu->Status.VerticalResolution = Instruction >> 2;
    Gpu->Status.VideoMode = Instruction >> 3;
    Gpu->Status.DisplayRGB24 = Instruction >> 4;
    Gpu->Status.InterlaceEnable = Instruction >> 5;

    /* bit 7 (Reverse flag 14) is unused here, catch the moment if it's set */
    if (Instruction & (1 << 7))
    {
        TODO("Implement bit 14 of GPUStat\n");
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



static void PS1_DoDMATransferBlock(PS1 *Ps1, DMA_Port Port)
{
    static const char *DMADeviceName[] = {
        "MDECin", "MDECout",
        "GPU", "CDROM",
        "SPU", "PIO",
        "OTC"
    };
    DMA_Chanel *Chanel = &Ps1->Dma.Chanels[Port];
    int Increment = 
        Chanel->Ctrl.Decrement
        ? -4 : 4;
    u32 Addr = Chanel->BaseAddr;
    u32 WordsLeft = DMA_GetChanelTransferSize(Chanel);

    if (Chanel->Ctrl.RamToDevice)
    {
        LOG("[DMA Transfer]: ram to device %d (%s):\n"
            "    Addr: %08x..%08x\n"
            "    Incr: %d\n"
            "    Size: %08x (%d) words\n",
            Port, DMADeviceName[Port], 
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
                GPU_WriteGP0(&Ps1->Gpu, Data);
                //LOG("      | %08x\n", Data);
            } break;
            default:
            {
                TODO("DMA from ram to device %d (%s)", Port, DMADeviceName[Port]);
            } break;
            }

            Addr += Increment;
            WordsLeft--;
        } while (WordsLeft != 0);
    }
    else /* device to ram */
    {
        LOG("[DMA Transfer]: device %d (%s) to ram:\n"
            "    Addr: %08x..%08x\n"
            "    Incr: %d\n"
            "    Size: %08x (%d) words\n",
            Port, DMADeviceName[Port], 
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
            case DMA_PORT_OTC: /* clear linked list */
            {
                /* current entry = prev entry */
                SrcWord = (Addr - 4) % PS1_RAM_SIZE;
                if (WordsLeft == 1) /* last entry */
                    SrcWord = 0xFFFFFF;
            } break;
            default:
            {
                TODO("DMA transfer from device %d (%s) to ram", Port, DMADeviceName[Port]);
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
            GPU_WriteGP0(&Ps1->Gpu, GPUCommand);
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



void PS1_Reset(PS1 *Ps1)
{
    CPU_Reset(&Ps1->Cpu, Ps1);
    GPU_Reset(&Ps1->Gpu, Ps1);
    DMA_Reset(&Ps1->Dma, Ps1);
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



u32 PS1_Read32(PS1 *Ps1, u32 LogicalAddr)
{
    if (LogicalAddr & 3)
    {
        TODO("unaligned load32: %08x", LogicalAddr);
    }

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
    if ((Translation = InRamRange(PhysicalAddr)).Valid)
    {
        u32 Offset = Translation.Offset;
        ASSERT(Offset + 4 <= PS1_RAM_SIZE && Offset < Offset + 4);

        PS1_Ram_Read32(Ps1, Offset, &Data);
        return Data; /* no log */
    }

    LOG("Read32 [%08x] ", LogicalAddr);
    if ((Translation = InMemCtrl1Range(PhysicalAddr)).Valid)
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
        LOG("(interrupt ctrl): %08x\n", Data);
    }
    else if ((Translation = InDMARange(PhysicalAddr)).Valid)
    {
        Data = DMA_Read32(&Ps1->Dma, Translation.Offset);
        LOG("(DMA): %08x\n", Data);
        return Data;
    }
    else if ((Translation = InGPURange(PhysicalAddr)).Valid)
    {
        if (PhysicalAddr == 0x1F801810) /* GPUREAD register, read only */
        {
            Data = GPU_ReadGPU(&Ps1->Gpu);
            LOG("(GPUREAD): %08x\n", Data);
        }
        else /* GPUSTAT register, read only */
        {
            Data = GPU_ReadStatus(&Ps1->Gpu);
            LOG("(GPUSTAT): %08x\n", Data);
        }
    }
    else if ((Translation = InTimerRange(PhysicalAddr)).Valid)
    {
        LOG("(timer): %08x\n", Data);
    }
    else
    {
        TODO("\nRead32 unknown region [%08x]\n", PhysicalAddr);
    }
    return Data;
}

u16 PS1_Read16(PS1 *Ps1, u32 LogicalAddr)
{
    u32 Data = 0;
    if (LogicalAddr & 1)
    {
        TODO("read16 unaligned: %08x\n", LogicalAddr);
    }

    u32 PhysicalAddr = PS1_GetPhysicalAddr(LogicalAddr);
    TranslatedAddr Translation = InRamRange(PhysicalAddr);
    if (Translation.Valid)
    {
        u32 Offset = Translation.Offset;
        ASSERT(Offset + 2 <= PS1_RAM_SIZE && Offset + 2 > Offset);

        Data = Ps1->Ram[Offset + 0];
        Data |= (u16)Ps1->Ram[Offset + 1] << 8;
        return Data; /* no log */
    }
    if ((Translation = InSPURange(PhysicalAddr)).Valid)
    {
        return Data; /* too much logging from spu */
    }

    LOG("Read16 [%08x] ", LogicalAddr);
    if ((Translation = InInterruptCtrlRange(PhysicalAddr)).Valid)
    {
        /* nop */
        LOG("(interrupt ctrl): TODO\n");
    }
    else
    {
        TODO("(unknown region)\n");
    }
    return Data;
}

u8 PS1_Read8(PS1 *Ps1, u32 LogicalAddr)
{
    u8 Data = 0;
    u32 PhysicalAddr = PS1_GetPhysicalAddr(LogicalAddr);
    TranslatedAddr Translation = InBiosRange(PhysicalAddr);
    if (Translation.Valid)
    {
        Data = Ps1->Bios[Translation.Offset];
        return Data; /*  no log for bios (too many reads) */
    }
    else if ((Translation = InRamRange(PhysicalAddr)).Valid)
    {
        Data = Ps1->Ram[Translation.Offset];
        return Data; /*  too much ram reads */
    }

    LOG("Read8 [%08x] ", LogicalAddr);
    if ((Translation = InExpansion1Range(PhysicalAddr)).Valid)
    {
        LOG("(Expansion 1)\n");
        Data = 0xFF;
    }
    else
    {
        LOG("(UNknown region)\n");
    }
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

    u32 PhysicalAddr = PS1_GetPhysicalAddr(LogicalAddr);
    TranslatedAddr Translation = InRamRange(PhysicalAddr);
    if (Translation.Valid)
    {
        u32 Offset = Translation.Offset;
        ASSERT(Offset + 4 <= PS1_RAM_SIZE && Offset < Offset + 4);

        PS1_Ram_Write32(Ps1, Offset, Data);
        return; /*  ram log is unnecessary since there is going to be a lot of ram write */
    }

    LOG("Write32 [%08x] ", LogicalAddr);
    if ((Translation = InMemCtrl1Range(PhysicalAddr)).Valid)
    {
        LOG("(memctrl)\n");
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
        LOG("(memctrl 2)\n");
        if (Data != 0x00000B88)
        {
            LOG("\nUnexpected RAM_SIZE value: %08x (expected %08x)\n", Data, 0x00000B88);
            ASSERT(Data == 0x00000B88);
        }
    }
    else if ((Translation = InCacheCtrlRange(PhysicalAddr)).Valid)
    {
        /* nop */
        LOG("(cache ctrl)\n");
    }
    else if ((Translation = InInterruptCtrlRange(PhysicalAddr)).Valid)
    {
        /* nop */
        LOG("(interrupt ctrl)\n");
    }
    else if ((Translation = InTimerRange(PhysicalAddr)).Valid)
    {
        /* nop */
        LOG("(timer)\n");
    }
    else if ((Translation = InDMARange(PhysicalAddr)).Valid)
    {
        /* no log, too much data */
        LOG("(DMA): %08x\n", Data);
        DMA_Write32(&Ps1->Dma, Translation.Offset, Data);
        return;
    }
    else if ((Translation = InGPURange(PhysicalAddr)).Valid)
    {
        /* nop */
        if (PhysicalAddr == 0x1F801810) /* GP0 register (write only) */
        {
            LOG("(GP0): %08x\n", Data);
            GPU_WriteGP0(&Ps1->Gpu, Data);
        }
        else /* GP1 register, write only */
        {
            LOG("(GP1): %08x\n", Data);
            GPU_WriteGP1(&Ps1->Gpu, Data);
        }
    }
    else
    {
        TODO("(unknown) <- %08x", Data);
    }
}

void PS1_Write16(PS1 *Ps1, u32 LogicalAddr, u16 Data)
{
    if (LogicalAddr & 1)
    {
        TODO("Unaligned write16: [%08x] <- %04x", LogicalAddr, Data);
    }

    u32 PhysicalAddr = PS1_GetPhysicalAddr(LogicalAddr);
    TranslatedAddr Translation = InRamRange(PhysicalAddr);
    if (Translation.Valid)
    {
        u32 Offset = Translation.Offset;
        ASSERT(Offset + 2 > Offset && Offset + 2 <= PS1_RAM_SIZE);

        Ps1->Ram[Offset + 0] = Data;
        Ps1->Ram[Offset + 1] = Data >> 8;
        return;
    }
    else if ((Translation = InSPURange(PhysicalAddr)).Valid)
    {
        return; /* too much logging from spu */
    }

    LOG("Write16 [%08x] ", LogicalAddr);
    if ((Translation = InInterruptCtrlRange(PhysicalAddr)).Valid)
    {
        LOG("(interrupt ctrl): %08x\n", Data);
    }
    else if ((Translation = InTimerRange(PhysicalAddr)).Valid)
    {
        LOG("(timer): %08x\n", Data);
    }
    else
    {
        TODO("write16: [%08x] <- %04x", LogicalAddr, Data);
    }
}

void PS1_Write8(PS1 *Ps1, u32 LogicalAddr, u8 Data)
{
    u32 PhysicalAddr = PS1_GetPhysicalAddr(LogicalAddr);
    TranslatedAddr Translation = InRamRange(PhysicalAddr);
    if (Translation.Valid)
    {
        Ps1->Ram[Translation.Offset] = Data;
        return; /*  too much ram writes */
    }

    LOG("Write8 [%08x] ", LogicalAddr);
    if ((Translation = InExpansion2Range(PhysicalAddr)).Valid)
    {
        LOG("(expansion 2): %08x\n", Data);
    }
    else
    {
        TODO("write8: [%08x] <- %02x", LogicalAddr, Data);
    }
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

