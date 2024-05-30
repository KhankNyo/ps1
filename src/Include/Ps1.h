#ifndef PS1_H
#define PS1_H

#include "Common.h"
#include "CPU.h"

struct PS1
{
#define PS1_BIOS_SIZE (512 * KB)
#define PS1_RAM_SIZE (2 * MB)
    u8 *Bios;
    u8 *Ram;
    CPU Cpu;
};

u32 PS1_Read32(PS1 *, u32 Addr);
u16 PS1_Read16(PS1 *, u32 Addr);
u8 PS1_Read8(PS1 *, u32 Addr);
void PS1_Write32(PS1 *, u32 Addr, u32 Data);
void PS1_Write16(PS1 *, u32 Addr, u16 Data);
void PS1_Write8(PS1 *, u32 Addr, u8 Data);



#endif /* PS1_H */

