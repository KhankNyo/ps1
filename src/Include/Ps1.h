#ifndef PS1_H
#define PS1_H

#include "Common.h"
#include "CPU.h"


typedef enum 
{
    DMA_PORT_MDEC_IN = 0,
    DMA_PORT_MDEC_OUT,
    DMA_PORT_GPU,
    DMA_PORT_CDROM,
    DMA_PORT_SPU,
    DMA_PORT_PIO,
    DMA_PORT_OTC,
} DMA_Port;

typedef enum 
{
    DMA_SYNCMODE_MANUAL = 0,
    DMA_SYNCMODE_REQUEST,
    DMA_SYNCMODE_LINKEDLIST,
} DMA_SyncMode;

typedef struct
{
    unsigned RamToDevice:1;         /* 0 */
    unsigned Decrement:1;           /* 1 */
    unsigned ChoppingEnable:1;      /* 2 */
    unsigned SyncMode:2;            /* 9..10: 0=Manual, 1=Request, 2=LinkedList (GPU) */
    unsigned ChoppingDMAWindow:3;   /* 16..18: shift count for num words */
    unsigned ChoppingCPUWindow:3;   /* 20..22: shift count for num cycles */
    unsigned Enable:1;              /* 24: also called Start/Busy */
    unsigned ManualTrigger:1;       /* 28 */
    unsigned Unknown:2;             /* 29..30 */
} DMA_ChanelCtrl;

typedef struct 
{
    unsigned Unknown:5;
    unsigned ForceIRQ:1;
    unsigned IRQChanelEnable:7;
    unsigned IRQMasterEnable:1;
    unsigned IRQChanelFlags:7;
    unsigned IRQMasterFlag:1;
} DMA_InterruptCtrl;

typedef struct 
{
    u32 BaseAddr;           /* DADR */

    u16 BlockSize;          /* DBCR */
    u16 BlockAmount;

    DMA_ChanelCtrl Ctrl;   /* DCHCR */
} DMA_Chanel;

typedef struct
{
    DMA_Chanel Chanels[7];

    u32 PriorityCtrlReg;                /* DPCR */
    DMA_InterruptCtrl InterruptCtrlReg; /* DICR */

    PS1 *Bus;
} DMA;

static inline Bool8 DMA_IsChanelActive(const DMA_ChanelCtrl *Chanel)
{
    /* only need to check trigger bit when sync mode is 0 (manual) */
    Bool8 TriggerSet = 
        Chanel->SyncMode != DMA_SYNCMODE_MANUAL
        || Chanel->ManualTrigger;
    return Chanel->Enable && TriggerSet;
}



struct PS1
{
#define PS1_BIOS_SIZE (512 * KB)
#define PS1_RAM_SIZE (2 * MB)
    u8 *Bios;
    u8 *Ram;
    CPU Cpu;
    DMA Dma;
};

void PS1_DoDMATransfer(PS1 *, DMA_Port Chanel);
u32 PS1_Read32(PS1 *, u32 Addr);
u16 PS1_Read16(PS1 *, u32 Addr);
u8 PS1_Read8(PS1 *, u32 Addr);
void PS1_Write32(PS1 *, u32 Addr, u32 Data);
void PS1_Write16(PS1 *, u32 Addr, u16 Data);
void PS1_Write8(PS1 *, u32 Addr, u8 Data);



#endif /* PS1_H */

