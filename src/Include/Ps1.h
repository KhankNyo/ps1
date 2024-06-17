#ifndef PS1_H
#define PS1_H

#include "Common.h"
#include "CPU.h"
#include "DMA.h"
#include <wchar.h>


typedef struct 
{
    /* single bit: 1/0 */
    unsigned TexturePageX:4;            /* 0..3 N*64 */
    unsigned TexturePageY:1;            /* 4    256/0 */
    unsigned SemiTransparency:2;        /* 5..6 */
    unsigned TextureDepth:2;            /* 7..8 0=4, 1=8, 2=15, 3=reserved bits */
    unsigned DitherEnable:1;            /* 9    enable/disable dither 24 bit RGB to 15 bit RGB */
    unsigned DrawEnable:1;              /* 10   enable/disable drawing to display area */
    unsigned SetMaskBitOnDraw:1;        /* 11   set/don't set mask bit (15) when drawing a pixel*/
    unsigned PreserveMaskedPixel:1;     /* 12   always/don't draw to pixel with mask bit (15) */
    unsigned Field:1;                   /* 13   always 1 when GP1(08h).5=0 */
    // unsigned ReverseFlags:1;         /* 14   */
    unsigned TextureDisable:1;          /* 15   disable/enable texture */
    unsigned HorizontalResolution:3;    /* 16.18 xx1: 368; 000: 256; 010: 320; 100: 512; 110: 640 */
    unsigned VerticalResolution:1;      /* 19    480/240 */
    unsigned VideoMode:1;               /* 20   PAL/NTSC */
    unsigned DisplayRGB24:1;            /* 21   24/15 bbp */
    unsigned InterlaceEnable:1;         /* 22   enable/disable interlacing */
    unsigned DisplayDisable:1;          /* 23   disable/enable display */
    unsigned Interrupt:1;               /* 24   IRQ/off */
    /* bit 25 is derived from other bits, no need to store it */
    unsigned ReadyToReceiveCmdWord:1;   /* 26 */
    unsigned ReadyToSend:1;             /* 27 */
    unsigned ReadyToReceiveDMABlock:1;  /* 28 */
    unsigned DMADirection:2;            /* 29..30  00: off; 01: fifo; 10: CPU to GP0; 11: GPURead to CPU */
} GPUStat;

typedef struct GPU
{
    u32 CommandBuffer[16];
    uint CommandBufferSize;
    uint CommandWordsRemain;
    void (*CommandBufferFn)(struct GPU *);

    GPUStat Status;
    PS1 *Bus;

    Bool8 TexturedRectangleXFlip;
    Bool8 TexturedRectangleYFlip;

    u8 TextureWindowMaskX;
    u8 TextureWindowMaskY;
    u8 TextureWindowOffsetX;
    u8 TextureWindowOffsetY;

    u16 DrawingAreaLeft;
    u16 DrawingAreaRight;
    u16 DrawingAreaTop;
    u16 DrawingAreaBottom;
    i16 DrawingOffsetX;
    i16 DrawingOffsetY;

    u16 DisplayVRAMStartX;
    u16 DisplayVRAMStartY;
    u16 DisplayHorizontalStart;
    u16 DisplayHorizontalEnd;
    u16 DisplayLineStart;
    u16 DisplayLineEnd;
} GPU;



struct PS1
{
#define PS1_BIOS_SIZE (512 * KB)
#define PS1_RAM_SIZE (2 * MB)
    u8 *Bios;
    u8 *Ram;
    CPU Cpu;
    GPU Gpu;
    DMA Dma;
};

void PS1_DoDMATransfer(PS1 *, DMA_Port Chanel);
#define PS1_Ram_Write32(ps1_ptr, addr, u32val) do {\
    u32 v = u32val;\
    memcpy((ps1_ptr)->Ram + (addr), &v, sizeof(u32));\
} while (0) 
#define PS1_Ram_Read32(ps1_ptr, addr, out_u32ptr) \
    memcpy(out_u32ptr, (ps1_ptr)->Ram + (addr), sizeof(u32));
u32 PS1_Read32(PS1 *, u32 Addr);
u16 PS1_Read16(PS1 *, u32 Addr);
u8 PS1_Read8(PS1 *, u32 Addr);
void PS1_Write32(PS1 *, u32 Addr, u32 Data);
void PS1_Write16(PS1 *, u32 Addr, u16 Data);
void PS1_Write8(PS1 *, u32 Addr, u8 Data);



#endif /* PS1_H */

