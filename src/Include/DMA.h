#ifndef DMA_H
#define DMA_H

#include "Common.h"

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


void DMA_Reset(DMA *Dma, PS1 *Bus);
u32 DMA_Read32(DMA *Dma, u32 Offset);
void DMA_Write32(DMA *Dma, u32 Offset, u32 Data);
u32 DMA_GetChanelTransferSize(const DMA_Chanel *Chanel);
void DMA_SetTransferFinishedState(DMA_Chanel *Chanel);

#endif /* DMA_H */
