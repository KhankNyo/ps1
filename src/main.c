#include <stdio.h>
#include "R3000A.h"

typedef struct PS1 
{
    R3000A CPU;
} PS1;

static u32 Ps1_ReadFn(void *UserData, u32 Addr, R3000A_DataSize Size)
{
}

static void Ps1_WriteFn(void *UserData, u32 Addr, u32 Data, R3000A_DataSize Size)
{
}

static Bool8 Ps1_VerifyAddr(void *UserData, u32 Addr)
{
}

int main(void)
{
    PS1 Ps1;
    Ps1.CPU = R3000A_Init(
        &Ps1, 
        Ps1_ReadFn, 
        Ps1_WriteFn, 
        Ps1_VerifyAddr, 
        Ps1_VerifyAddr
    );
    return 0;
}

