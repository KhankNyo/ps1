
#include "R3000A.h"
#include "Disassembler.h"

#include <stdio.h>
#include <string.h>
#define KS1_BASE 0xBFC00000

typedef struct PS1 
{
    R3000A CPU;
    u8 Rom[512 * KB];
} PS1;

static u32 Ps1_ReadFn(void *UserData, u32 Addr, R3000A_DataSize Size)
{
    PS1 *Ps1 = UserData;
    if (IN_RANGE(KS1_BASE, Addr, KS1_BASE + 512*KB))
    {
        u32 PhysicalAddr = Addr - KS1_BASE;
        u32 Data = 0;
        for (int i = 0; i < (int)Size; i++)
        {
            Data |= (u32)Ps1->Rom[PhysicalAddr + i] << i*8;
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
    (void)UserData, (void)Size;
    if (IN_RANGE(0x1F801000, Addr, 0x1F801020)) /* mem ctrl 1 */
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
            if (IN_RANGE(KS1_BASE, CurrentPC, KS1_BASE + sizeof Ps1->Rom))
            {
                u32 PhysAddr = CurrentPC - KS1_BASE;
                u32 Instruction;
                memcpy(&Instruction, Ps1->Rom + PhysAddr, sizeof Instruction);
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
    const char *FileName = argv[1];
    FILE *RomFile = fopen(FileName, "rb");
    {
        fseek(RomFile, 0, SEEK_END);
        iSize FileSize = ftell(RomFile);
        fseek(RomFile, 0, SEEK_SET);

        if (FileSize != sizeof Ps1.Rom)
        {
            printf("Error: rom size must be exactly %d KB\n", (int)(sizeof Ps1.Rom)/KB);
            goto CloseRom;
        }
        int ReadSize = fread(Ps1.Rom, 1, sizeof Ps1.Rom, RomFile);
        if (ReadSize != sizeof Ps1.Rom)
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
    return 0;

CloseRom:
    fclose(RomFile);
    return 1;
}

