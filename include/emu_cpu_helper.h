#pragma once

#ifndef EMU_CPU_HELPER_H
#define EMU_CPU_HELPER_H

#include "alu_binary.h"

#define READ_U16(addr) (((u16)read((addr) + 1) << 8) | read(addr))

void inc_u8(u8* a) {
    F_H = HALF_CARRY_U8_ADD(*a, 1);
    F_N = 0;
    (*a)++;
    F_Z = (*a == 0);
}
void dec_u8(u8* a) {
    F_H = HALF_CARRY_U8_SUB(*a, 1);
    F_N = 1;
    (*a)--;
    F_Z = (*a == 0);
}
void add_u8(u8* a, u8* b) {
    F_H = HALF_CARRY_U8_ADD(*a, *b);
    F_C = CARRY_ADD(*a, *b);
    F_N = 0;
    (*a) += (*b);
    F_Z = (*a == 0);
}
void add_u16(u16* a, u16* b) {
    F_H = HALF_CARRY_U16_ADD(*a, *b);
    F_C = CARRY_ADD(*a, *b);
    F_N = 0;
    (*a) += (*b);
}
void sub_u8(u8* b) {
    F_H = HALF_CARRY_U8_SUB(A, *b);
    F_C = CARRY_ADD(A, *b);
    F_N = 1;
    A -= *b;
    F_Z = (A == 0);
}
/*
#define INC_U8(a) \
    do{ \
        F_H = HALF_CARRY_U8_ADD(a, 1); \
        a++; \
        F_Z = (a == 0); \
        F_N = 0; \
    } while (0)

#define DEC_U8(a) \
    do{ \
        F_H = HALF_CARRY_U8_SUB(a, 1); \
        a--; \
        F_Z = (a == 0); \
        F_N = 1; \
    } while (0)

#define ADD_U8(a, b) \
    do{ \
        F_H = HALF_CARRY_U8_ADD(a, b); \
        F_C = CARRY_ADD(a, b); \
        F_N = 0; \
        a += b; \
        F_Z = (a == 0); \
    } while (0)
#define ADD_U16(a, b) \
    do{ \
        F_H = HALF_CARRY_U16_ADD(a, b); \
        F_C = CARRY_ADD(a, b); \
        F_N = 0; \
        a += b; \
    } while (0)

#define SUB_U8(b) \
    do{ \
        F_H = HALF_CARRY_U8_SUB(A, b); \
        F_C = CARRY_ADD(A, b); \
        F_N = 1; \
        A -= b; \
        F_Z = (A == 0); \
    } while (0)
*/

const int MAXDOTS = 70224; // 154 scanlines per frame, 456 dots per scanline = 70224 (59.7275 FPS)
const int SCANLINE_DOTS = 456; // dots per scanline
/* Scan lines 0~143 consists of :
MODE 2 (80 dots) OAM scan
MODE 3 (172~289 dots) drawing pixels
MODE 0 (86~204 dots) horizontal blank

Scan lines 144~153:
MODE 1 (Scanlines 144~153) Vertical blank - everything is accessible

In MODE 2+3 - OAM inaccessible (except by DMA)
In MODE 3 - VRAM ($8000–9FFF) inaccessible, CGB palettes inaccessible

When an action lengthens MODE 3 it means that MODE 0 is shortened by the same amount */

enum MemoryMap {
    MEM_ROM_0 = 0x0000, // [0000~3FFF] 16 KiB ROM bank 00 (From cartridge, usually a fixed bank)
    MEM_ROM_N = 0x4000, // [4000~7FFF] 16 KiB ROM Bank 01~NN (From cartridge, switchable bank via mapper (if any))
    MEM_VRAM = 0x8000, // [8000~9FFF] 8 KiB Video RAM (VRAM) (In CGB mode, switchable bank 0/1)
    MEM_ERAM = 0xA000, // [A000~BFFF] 8 KiB External RAM (From cartridge, switchable bank if any)
    MEM_WRAM = 0xC000, // [C000~CFFF] 4 KiB Work RAM (WRAM) (Volatile - for temporary storage)
    MEM_WRAM_N = 0xD000, // [D000~DFFF] Used together with WRAM_0 as a contiguous block. In CGB mode, switchable bank 1~7.
    MEM_ECHORAM = 0xE000, // [E000~FDFF] Mirror of WRAM C000~DDFF (ECHO RAM) (use of this area is prohibited)
    MEM_OAM = 0xFE00, // [FE00~FE9F] Sprite information for displaying on screen
    MEM_UNUSABLE = 0xFEA0, // [FEA0~FEFF] Use of this area is prohibited
    MEM_IO = 0xFF00, // [FF00~FF7F] I/O Registers
    MEM_HRAM = 0xFF80, // [FF80~FFFE] 127 Bytes High RAM (HRAM)
    MEM_IE = 0xFFFF, // [FFFF]      Interrupt Enable register (IE)
};
enum IO {
    IO_JOYPAD = 0xFF00, // [FF01]      Joypad input
    IO_SERIAL = 0xFF01, // [FF01~FF02] Serial transfer. FF01 (SB Data Register), FF02 (SC Control Register)
    IO_TIMER_DIV = 0xFF04, // [FF04~FF07] Timer and Divider
    IO_AUDIO = 0xFF10, // [FF10~FF26] Audio
    IO_WAVE = 0xFF30, // [FF30~FF3F] Wave pattern
    IO_LCD = 0xFF40, // [FF40~FF4B] LCD Control, Status, Position, Scrolling, and Palettes
    IO_VRAM_BANK = 0xFF4F, // [FF4F]      VRAM Bank Select, only bit 0 matters
    IO_BOOT_ROM = 0xFF50, // [FF50]      Set to non-zero to disable boot ROM
    IO_VRAM_DMA = 0xFF51, // [FF51~FF55] VRAM DMA
    IO_PALETTES = 0xFF68, // [FF68~FF6B] BG / OBJ Palettes
    IO_WRAM_BANK = 0xFF70, // [FF70]      WRAM Bank Select
};
enum Register {
    REG_P1 = 0x00, // Joypad
    REG_SB = 0X01, // Serial transfer data
    REG_SC = 0X02, // Serial transfer control
    REG_DIV = 0X04, // Divider register
    REG_TIMA = 0X05, // Timer counter
    REG_TMA = 0X06, // Timer modulo
    REG_TAC = 0X07, // Timer control
    REG_IF = 0X0F, // Interrupt flag
    REG_NR10 = 0X10, // Sound channel 1 sweep	
    REG_NR11 = 0X11, // Sound channel 1 length timer & duty cycle
    REG_NR12 = 0X12, // Sound channel 1 volume & envelope
    REG_NR13 = 0X13, // Sound channel 1 period low
    REG_NR14 = 0X14, // Sound channel 1 period high & control
    REG_NR21 = 0X16, // Sound channel 2 length timer & duty cycle
    REG_NR22 = 0X17, // Sound channel 2 volume & envelope
    REG_NR23 = 0X18, // Sound channel 2 period low
    REG_NR24 = 0X19, // Sound channel 2 period high & control
    REG_NR30 = 0X1A, // Sound channel 3 DAC enable
    REG_NR31 = 0X1B, // Sound channel 3 length timer
    REG_NR32 = 0X1C, // Sound channel 3 output level
    REG_NR33 = 0X1D, // Sound channel 3 period low
    REG_NR34 = 0X1E, // Sound channel 3 period high & control
    REG_NR41 = 0X20, // Sound channel 4 length timer
    REG_NR42 = 0X21, // Sound channel 4 volume & envelope
    REG_NR43 = 0X22, // Sound channel 4 frequency & randomness
    REG_NR44 = 0X23, // Sound channel 4 control
    REG_NR50 = 0X24, // Master volume & VIN panning
    REG_NR51 = 0X25, // Sound panning
    REG_NR52 = 0X26, // Sound on/off
    REG_WAVERAM = 0X30, // Storage for one of the sound channels’ waveform
    REG_LCDC = 0X40, // LCD control
    REG_STAT = 0X41, // LCD status
    REG_SCY = 0X42, // Viewport Y position
    REG_SCX = 0X43, // Viewport X position
    REG_LY = 0X44, // LCD Y coordinate
    REG_LYC = 0X45, // LY compare
    REG_DMA = 0X46, // OAM DMA source address & start
    REG_BGP = 0X47, // BG palette data
    REG_OBP0 = 0X48, // OBJ palette 0 data
    REG_OBP1 = 0X49, // OBJ palette 1 data
    REG_WY = 0X4A, // Window Y position
    REG_WX = 0X4B, // Window X position plus 7
    REG_KEY1 = 0X4D, // Prepare speed switch
    REG_VBK = 0X4F, // VRAM bank
    REG_HDMA1 = 0X51, // VRAM DMA source high
    REG_HDMA2 = 0X52, // VRAM DMA source low
    REG_HDMA3 = 0X53, // VRAM DMA destination high
    REG_HDMA4 = 0X54, // VRAM DMA destination low
    REG_HDMA5 = 0X55, // VRAM DMA length/mode/start	
    REG_RP = 0X56, // Infrared communications port
    REG_BGPI = 0X68, // Background color palette specification / Background palette index
    REG_BGPD = 0X69, // Background color palette data / Background palette data
    REG_OBPI = 0X6A, // OBJ color palette specification / OBJ palette index
    REG_OBPD = 0X6B, // OBJ color palette data / OBJ palette data
    REG_OPRI = 0X6C, // Object priority mode
    REG_SVBK = 0X70, // WRAM bank
    REG_PCM12 = 0X76, // Audio digital outputs 1 & 2
    REG_PCM34 = 0X77, // Audio digital outputs 3 & 4
    REG_IE = 0XFF, // Interrupt enable
};
enum RomHeader {
    ROM_ENTRY = 0x100, // starting point after boot ROM
    ROM_LOGO = 0x104, // bitmap image that has to match the boot ROM's
    ROM_TITLE = 0x134, // ASCII title of the game
    ROM_MANUFACTURER = 0x13F, // 4-character manufacturer code
    ROM_CGB_FLAG = 0x143, // whether or not to enable CGB mode (0x80: monochrome+CGB, 0xC0: CGB only)
    ROM_LICENSEE_NEW = 0x144, // if old license is 0x33, uses this one instead
    ROM_SGB_FLAG = 0x146, // SuperGameBoy(SNES) mode: if 0x03, ignore any command packets
    ROM_CART_TYPE = 0x147, // what hardware is present
    ROM_ROM_SIZE = 0x148, // how much ROM is present (32 KiB ª (1 << <value>))
    ROM_RAM_SIZE = 0X149, // how much RAM is present, if any. Ignored if CART_TYPE has no RAM
    ROM_DESTINATION = 0x14A, // 0x00: Japan, 0x0: Global
    ROM_LICENSEE_OLD = 0x14B, // the SGB will ignore any command packets unless this value is 0x33
    ROM_VERSION = 0X14C, // version number of the game
    ROM_HEADER_CHECKSUM = 0x14D, // 8-bit checksum that gets verified by the boot ROM
    ROM_GLOBAL_CHECKSUM = 0x14E, // 16-bit checksum (not verified)
};
enum RTCRegister {
    RTC_S = 0x08, // Seconds (0x00-0x3B)
    RTC_M = 0x09, // Minutes (0x00-0x3B)
    RTC_H = 0x0A, // Hours (0x00-0x17)
    RTC_DL = 0x0B, // Lower 8 bits of Day Counter (0x00-0xFF)
    RTC_DH = 0x0C, // b0-Upper 1 bit of Day Counter, b6-Halt Flag, b7-Carry Bit
};
#define BANKSIZE_ROM    0x4000
#define BANKSIZE_ERAM   0x2000
#define BANKSIZE_VRAM   0x2000
#define BANKSIZE_WRAM   0x1000

// Determines how many CPU cycles each instruction takes to perform
u8 op_cycles_lut[] = {
     4,12, 8, 8, 4, 4, 8, 4,20, 8, 8, 8, 4, 4, 8, 4,
     4,12, 8, 8, 4, 4, 8, 4,12, 8, 8, 8, 4, 4, 8, 4,
    12,12, 8, 8, 4, 4, 8, 4,12, 8, 8, 8, 4, 4, 8, 4,
    12,12, 8, 8,12,12,12, 4,12, 8, 8, 8, 4, 4, 8, 4,
     4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
     4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
     4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
     8, 8, 8, 8, 8, 8, 4, 8, 4, 4, 4, 4, 4, 4, 8, 4,
     4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
     4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
     4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
     4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
    20,12,16,16,24,16, 8,16,20,16,16, 4,24,24, 8,16,
    20,12,16, 0,24,16, 8,16,20,16,16, 0,24, 0, 8,16,
    12,12, 8, 0, 0,16, 8,16,16, 4,16, 0, 0, 0, 8,16,
    12,12, 8, 4, 0,16, 8,16,12, 8,16, 4, 0, 0, 8,16
};

// In Prefix CB table: all op are 2 bytes long and take 8 cycles to perform
// The only exception is in op: 0x_6 / 0x_E where its 16 cycles.

#endif EMU_CPU_HELPER_H