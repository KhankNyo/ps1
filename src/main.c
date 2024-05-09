
#include "R3000A.h"
#include "Disassembler.h"

#include <stdio.h>
#include <string.h>


typedef struct PS1 
{
    R3000A CPU;
    u32 BiosRomSize;
    u32 RamSize;
    u8 *BiosRom;
    u8 *Ram;
} PS1;

#define KUSEG 0
#define KSEG0 0x80000000
#define KSEG1 0xA0000000 

#define MAIN_RAM(Seg_)      ((Seg_) + 0)
#define EXPANSION_1(Seg_)   ((Seg_) + 0x1F000000)
#define IO_PORTS(Seg_)      ((Seg_) + 0x1F801000)
#define EXPANSION_2(Seg_)   ((Seg_) + 0x1F802000)
#define EXPANSION_3(Seg_)   ((Seg_) + 0x1FA00000)
#define BIOS_ROM(Seg_)      ((Seg_) + 0x1FC00000)

#define ACCESS_BIOS_ROM(Seg_, Addr_)    IN_RANGE(BIOS_ROM(Seg_), Addr_, BIOS_ROM(Seg_) + 512*KB)
#define ACCESS_RAM(Seg_, Addr_)         IN_RANGE(MAIN_RAM(Seg_), Addr_, MAIN_RAM(Seg_) + 2*MB)


static u32 Ps1_TranslateAddr(PS1 *Ps1, u32 Addr)
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


static u32 Ps1_ReadFn(void *UserData, u32 Addr, R3000A_DataSize Size)
{
    PS1 *Ps1 = UserData;

    if (Addr > 0xFFFE0000) /* memctrl IO ports (kseg2) */
    {
        printf("Reading from %08x: (memctrl)\n", Addr);
        return 0;
    }

    u32 PhysicalAddr = Ps1_TranslateAddr(Ps1, Addr); 
    if (ACCESS_BIOS_ROM(0, PhysicalAddr))
    {
        PhysicalAddr -= BIOS_ROM(0);

        u32 Data = 0;
        for (int i = 0; 
            i < (int)Size 
            && PhysicalAddr + i < Ps1->BiosRomSize; 
            i++)
        {
            Data |= (u32)Ps1->BiosRom[PhysicalAddr + i] << i*8;
        }
        return Data;
    }
    else if (ACCESS_RAM(0, PhysicalAddr))
    {
        PhysicalAddr -= MAIN_RAM(0);

        u32 Data = 0;
        for (int i = 0; 
            i < (int)Size 
            && PhysicalAddr + i < Ps1->RamSize; 
            i++)
        {
            Data |= (u32)Ps1->Ram[PhysicalAddr + i] << i*8;
        }
        printf("Reading %08x from %08x (ram)\n", Data, Addr);
        return Data;
    }
    else 
    {
        printf("Reading from %08x: (unknown)\n", Addr);
    }
    return 0;
}

static void Ps1_WriteFn(void *UserData, u32 Addr, u32 Data, R3000A_DataSize Size)
{
    PS1 *Ps1 = UserData;

    printf("Writing %08x to %08x ", Data, Addr);
    u32 PhysicalAddr = Ps1_TranslateAddr(Ps1, Addr);
    if (ACCESS_RAM(0, PhysicalAddr))
    {
        PhysicalAddr -= MAIN_RAM(0);
        for (int i = 0; 
            i < (int)Size
            && PhysicalAddr + i < Ps1->RamSize; 
            i++)
        {
            Ps1->Ram[PhysicalAddr + i] = Data >> i*8;
        }
        puts("(ram)");
    }
    else if (IN_RANGE(0x1F801000, Addr, 0x1F801020)) /* mem ctrl 1 */
    {
        puts("(memctrl 1)");
    }
    else if (Addr == 0x1F801060) /* mem ctrl 2 (ram size) */
    {
        puts("(memctrl 2 / RAM_SIZE)");
    }
    else
    {
        puts("(unknown)");
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
        .CPU = R3000A_Init(Ps1, Ps1_ReadFn, Ps1_WriteFn, Ps1_VerifyAddr, Ps1_VerifyAddr),
        .BiosRomSize = 512 * KB,
        .RamSize = 2 * MB,
    };

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




static void UpdateDisassembly(const R3000A *Mips)
{
#define NUM_INS 10
    typedef struct DisasmInfo 
    {
        char Opc[64];
        u32 Instruction;
        u32 Addr;
    } DisasmInfo;

    static DisasmInfo Disasm[NUM_INS] = { 0 };
    if (!IN_RANGE(Disasm[0].Addr, Mips->PC, Disasm[NUM_INS - 1].Addr))
    {
        const PS1 *Ps1 = Mips->UserData;
        u32 CurrentPC = Mips->PC;
        for (int i = 0; i < NUM_INS; i++)
        {
            if (ACCESS_BIOS_ROM(KSEG1, CurrentPC))
            {
                u32 PhysAddr = CurrentPC - BIOS_ROM(KSEG1);
                u32 Instruction;
                memcpy(&Instruction, Ps1->BiosRom + PhysAddr, sizeof Instruction);
                R3000A_Disasm(
                    Instruction, 
                    CurrentPC,
                    DISASM_IMM16_AS_HEX,
                    Disasm[i].Opc, 
                    sizeof Disasm[i].Opc
                );
                Disasm[i].Instruction = Instruction;
                Disasm[i].Addr = CurrentPC;
                CurrentPC += 4;
            }
            else
            {
                Disasm[i].Addr = CurrentPC;
                Disasm[i].Instruction = 0;
                snprintf(Disasm[i].Opc, sizeof Disasm[i].Opc, "???");
            }
        }
    }

    for (int i = 0; i < NUM_INS; i++)
    {
        const char *Pointer = "   ";
        if (Mips->PC == Disasm[i].Addr)
            Pointer = "PC>";
        else if (Mips->PC - 4*sizeof(Disasm[0].Instruction) == Disasm[i].Addr)
            Pointer = "wb>";
        printf( 
            "%s %08X:  %08X  %s\n", 
            Pointer, Disasm[i].Addr, Disasm[i].Instruction, Disasm[i].Opc
        );
    }
#undef NUM_INS
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

    Display = "========== Disassembly ==========";
    printf("%s\n", Display);

    UpdateDisassembly(Mips);
    LastState = *Mips;
}

static char GetCmdLine(const char *Prompt)
{
#if 0
    printf("%s", Prompt);
    char Tmp[256];
    fgets(Tmp, sizeof Tmp, stdin);
    return Tmp[0];
#endif 
    return 0;
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

    Ps1.CPU = R3000A_Init(
        &Ps1, 
        Ps1_ReadFn, 
        Ps1_WriteFn, 
        Ps1_VerifyAddr, 
        Ps1_VerifyAddr
    );
    do {
        //DumpState(&Ps1.CPU);
        R3000A_StepClock(&Ps1.CPU);
    } while ('q' != GetCmdLine("Press Enter to cont...\n"));
    Ps1_Destroy(&Ps1);
    return 0;

CloseRom:
    fclose(RomFile);
    return 1;
}

