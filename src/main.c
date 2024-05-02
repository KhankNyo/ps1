
#include "R3000A.h"
#include "Disassembler.h"

#include <stdio.h>
#include <string.h>


typedef struct PS1 
{
    R3000A CPU;
    u32 BiosRomSize,
        RamSize;
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


static u32 Ps1_ReadFn(void *UserData, u32 Addr, R3000A_DataSize Size)
{
    PS1 *Ps1 = UserData;
    if (ACCESS_BIOS_ROM(KSEG1, Addr))
    {
        u32 PhysicalAddr = Addr - BIOS_ROM(KSEG1);
        u32 Data = 0;
        for (int i = 0; i < (int)Size; i++)
        {
            Data |= (u32)Ps1->BiosRom[PhysicalAddr + i] << i*8;
        }
        return Data;
    }
    else if (ACCESS_RAM(KSEG1, Addr))
    {
        u32 PhysicalAddr = Addr - MAIN_RAM(KSEG1);
        u32 Data = 0;
        for (int i = 0; i < (int)Size; i++)
        {
            Data |= (u32)Ps1->Ram[PhysicalAddr + i] << i*8;
        }
        return Data;
    }
    else
    {
        printf("Unknown read addr: %08x\n", Addr);
        return 0;
    }
}

static void Ps1_WriteFn(void *UserData, u32 Addr, u32 Data, R3000A_DataSize Size)
{
    PS1 *Ps1 = UserData;
    if (ACCESS_RAM(KSEG1, Addr))
    {
        u32 PhysicalAddr = Addr - MAIN_RAM(KSEG1);
        for (int i = 0; i < (int)Size; i++)
        {
            Ps1->Ram[PhysicalAddr + i] = Data >> i*8;
        }
    }
    else if (IN_RANGE(0x1F801000, Addr, 0x1F801020)) /* mem ctrl 1 */
    {
        printf("Writing %08x to %08x (memctrl 1)\n", Data, Addr);
    }
    else if (Addr == 0x1F801060) /* mem ctrl 2 (ram size) */
    {
        printf("Writing %08x to %08x (memctrl 2 - RAM_SIZE)\n", Data, Addr);
    }
    else
    {
        printf("Writing %08x to %08x (unknown)\n", Data, Addr);
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
    printf("%s", Prompt);
    char Tmp[256];
    fgets(Tmp, sizeof Tmp, stdin);
    return Tmp[0];
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
        DumpState(&Ps1.CPU);
        R3000A_StepClock(&Ps1.CPU);
    } while ('q' != GetCmdLine("Press Enter to cont...\n"));
    Ps1_Destroy(&Ps1);
    return 0;

CloseRom:
    fclose(RomFile);
    return 1;
}

