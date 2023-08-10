#include "emu_cpu.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "alu_helper.h"

/// <summary>
/// Emulates the Game Boy's DMG-CPU 
/// </summary>
/// 

// Core Registers
BytePair AF; // Accumulator & Flags
BytePair BC;
BytePair DE;
BytePair HL;
uint16_t SP; // Stack Pointer
uint16_t PC; // Program Counter/Pointer

const int MAXDOTS = 70224; // 154 scanlines per frame, 456 dots per scanline = 70224 (59.7275 FPS)
const int SCANLINE_DOTS = 456; // dots per scanline
// Scan lines 0~143 consists of:
// MODE 2 (80 dots) OAM scan
// MODE 3 (172~289 dots) drawing pixels
// MODE 0 (86~204 dots) horizontal blank
// 
// Scan lines 144~153:
// MODE 1 (Scanlines 144~153) Vertical blank - everything is accessible
// 
// In MODE 2+3 - OAM inaccessible (except by DMA)
// In MODE 3 - VRAM ($8000–9FFF) inaccessible, CGB palettes inaccessible

// When an action lengthens MODE 3 it means that MODE 0 is shortened by the same amount



enum MemoryMap {
    MEM_ROM_0           = 0x0000, // [0000~3FFF] 16 KiB ROM bank 00 (From cartridge, usually a fixed bank)
    MEM_ROM_SWITCHABLE  = 0x4000, // [4000~7FFF] 16 KiB ROM Bank 01~NN (From cartridge, switchable bank via mapper (if any))
    MEM_VRAM            = 0x8000, // [8000~9FFF] 8 KiB Video RAM (VRAM) (In CGB mode, switchable bank 0/1)
    MEM_ERAM            = 0xA000, // [A000~BFFF] 8 KiB External RAM (From cartridge, switchable bank if any)
    MEM_WRAM            = 0xC000, // [C000~CFFF] 4 KiB Work RAM (WRAM) (Volatile - for temporary storage)
    MEM_WRAM_SWITCHABLE = 0xD000, // [D000~DFFF] Used together with WRAM_0 as a contiguous block. In CGB mode, switchable bank 1~7.
    MEM_ECHORAM         = 0xE000, // [E000~FDFF] Mirror of WRAM C000~DDFF (ECHO RAM) (use of this area is prohibited)
    MEM_OAM             = 0xFE00, // [FE00~FE9F] Sprite information for displaying on screen
    MEM_UNUSABLE        = 0xFEA0, // [FEA0~FEFF] Use of this area is prohibited
    MEM_IO              = 0xFF00, // [FF00~FF7F] I/O Registers
    MEM_HRAM            = 0xFF80, // [FF80~FFFE] 127 Bytes High RAM (HRAM)
    MEM_IE              = 0xFFFF, // [FFFF]      Interrupt Enable register (IE)
};

enum IO {
    IO_JOYPAD       = 0xFF00, // [FF01]      Joypad input
    IO_SERIAL       = 0xFF01, // [FF01~FF02] Serial transfer. FF01 (SB Data Register), FF02 (SC Control Register)
    IO_TIMER_DIV    = 0xFF04, // [FF04~FF07] Timer and Divider
    IO_AUDIO        = 0xFF10, // [FF10~FF26] Audio
    IO_WAVE         = 0xFF30, // [FF30~FF3F] Wave pattern
    IO_LCD          = 0xFF40, // [FF40~FF4B] LCD Control, Status, Position, Scrolling, and Palettes
    IO_VRAM_BANK    = 0xFF4F, // [FF4F]      VRAM Bank Select, only bit 0 matters
    IO_BOOT_ROM     = 0xFF50, // [FF50]      Set to non-zero to disable boot ROM
    IO_VRAM_DMA     = 0xFF51, // [FF51~FF55] VRAM DMA
    IO_PALETTES     = 0xFF68, // [FF68~FF6B] BG / OBJ Palettes
    IO_WRAM_BANK    = 0xFF70, // [FF70]      WRAM Bank Select
};

enum Register {
    REG_JOYP    = 0xFF00, // Joypad
    REG_SB      = 0XFF01, // Serial transfer data
    REG_SC      = 0XFF02, // Serial transfer control
    REG_DIV     = 0XFF04, // Divider register
    REG_TIMA    = 0XFF05, // Timer counter
    REG_TMA     = 0XFF06, // Timer modulo
    REG_TAC     = 0XFF07, // Timer control
    REG_IF      = 0XFF0F, // Interrupt flag
    REG_NR10    = 0XFF10, // Sound channel 1 sweep	
    REG_NR11    = 0XFF11, // Sound channel 1 length timer & duty cycle
    REG_NR12    = 0XFF12, // Sound channel 1 volume & envelope
    REG_NR13    = 0XFF13, // Sound channel 1 period low
    REG_NR14    = 0XFF14, // Sound channel 1 period high & control
    REG_NR21    = 0XFF16, // Sound channel 2 length timer & duty cycle
    REG_NR22    = 0XFF17, // Sound channel 2 volume & envelope
    REG_NR23    = 0XFF18, // Sound channel 2 period low
    REG_NR24    = 0XFF19, // Sound channel 2 period high & control
    REG_NR30    = 0XFF1A, // Sound channel 3 DAC enable
    REG_NR31    = 0XFF1B, // Sound channel 3 length timer
    REG_NR32    = 0XFF1C, // Sound channel 3 output level
    REG_NR33    = 0XFF1D, // Sound channel 3 period low
    REG_NR34    = 0XFF1E, // Sound channel 3 period high & control
    REG_NR41    = 0XFF20, // Sound channel 4 length timer
    REG_NR42    = 0XFF21, // Sound channel 4 volume & envelope
    REG_NR43    = 0XFF22, // Sound channel 4 frequency & randomness
    REG_NR44    = 0XFF23, // Sound channel 4 control
    REG_NR50    = 0XFF24, // Master volume & VIN panning
    REG_NR51    = 0XFF25, // Sound panning
    REG_NR52    = 0XFF26, // Sound on/off
    REG_WAVERAM = 0XFF30, // Storage for one of the sound channels’ waveform
    REG_LCDC    = 0XFF40, // LCD control
    REG_STAT    = 0XFF41, // LCD status
    REG_SCY     = 0XFF42, // Viewport Y position
    REG_SCX     = 0XFF43, // Viewport X position
    REG_LY      = 0XFF44, // LCD Y coordinate
    REG_LYC     = 0XFF45, // LY compare
    REG_DMA     = 0XFF46, // OAM DMA source address & start
    REG_BGP     = 0XFF47, // BG palette data
    REG_OBP0    = 0XFF48, // OBJ palette 0 data
    REG_OBP1    = 0XFF49, // OBJ palette 1 data
    REG_WY      = 0XFF4A, // Window Y position
    REG_WX      = 0XFF4B, // Window X position plus 7
    REG_KEY1    = 0XFF4D, // Prepare speed switch
    REG_VBK     = 0XFF4F, // VRAM bank
    REG_HDMA1   = 0XFF51, // VRAM DMA source high
    REG_HDMA2   = 0XFF52, // VRAM DMA source low
    REG_HDMA3   = 0XFF53, // VRAM DMA destination high
    REG_HDMA4   = 0XFF54, // VRAM DMA destination low
    REG_HDMA5   = 0XFF55, // VRAM DMA length/mode/start	
    REG_RP      = 0XFF56, // Infrared communications port
    REG_BGPI    = 0XFF68, // Background color palette specification / Background palette index
    REG_BGPD    = 0XFF69, // Background color palette data / Background palette data
    REG_OBPI    = 0XFF6A, // OBJ color palette specification / OBJ palette index
    REG_OBPD    = 0XFF6B, // OBJ color palette data / OBJ palette data
    REG_OPRI    = 0XFF6C, // Object priority mode
    REG_SVBK    = 0XFF70, // WRAM bank
    REG_PCM12   = 0XFF76, // Audio digital outputs 1 & 2
    REG_PCM34   = 0XFF77, // Audio digital outputs 3 & 4
    REG_IE      = 0XFFFF, // Interrupt enable
};

uint8_t* memory;

int emu_cpu_init()
{
    memory = (uint8_t*)malloc(sizeof(uint8_t) * 0x10000);
    if (memory == NULL)
    {
        fprintf(stderr, "Failed to allocate memory for the cpu!\n");
        return -1;
    }

    //AF.high = GET_BIT(AF.high, 3);
    //RESET_BIT(AF.high, 3);
    //printf("AF: %04X\n", AF.full);
    //SET_BIT(AF.high, 3);
    //printf("AF: %04X\n", AF.full);


    return 0;
}

void emu_cpu_update()
{
    int cycles_this_update = 0;
    while (cycles_this_update < MAXDOTS)
    {
        int cycles = 4; // instead of 4, a value is returned by the operand instruction
        cycles_this_update += cycles;
        //UpdateTimers(cycles);
        //UpdateGraphics(cycles);
        //DoInterupts();
    }
    //printf("cpu cycles: %d\n", cycles_this_update);
    //printf("fps: %d, cpu: %d\n", timer_total, cycles_this_update);
}

void emu_cpu_cleanup()
{
    if (memory) free(memory);
}
