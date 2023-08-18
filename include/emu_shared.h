#pragma once

#ifndef EMU_SHARED_H
#define EMU_SHARED_H

#include "alu_binary.h"

//#define READ_U16(addr) ( read(addr) | ((u16)read((addr) + 1) << 8))

static const int MAXDOTS = 70224; // 154 scanlines per frame, 456 dots per scanline = 70224 (59.7275 FPS)
static const int SCANLINE_DOTS = 456; // dots per scanline

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
    REG_P1 = 0x00,      // Joypad
    REG_SB = 0X01,      // Serial transfer data
    REG_SC = 0X02,      // Serial transfer control
    REG_DIV = 0X04,     // Divider register
    REG_TIMA = 0X05,    // Timer counter
    REG_TMA = 0X06,     // Timer modulo
    REG_TAC = 0X07,     // Timer control
    REG_IF = 0X0F,      // Interrupt flag
    REG_NR10 = 0X10,    // Sound channel 1 sweep	
    REG_NR11 = 0X11,    // Sound channel 1 length timer & duty cycle
    REG_NR12 = 0X12,    // Sound channel 1 volume & envelope
    REG_NR13 = 0X13,    // Sound channel 1 period low
    REG_NR14 = 0X14,    // Sound channel 1 period high & control
    REG_NR21 = 0X16,    // Sound channel 2 length timer & duty cycle
    REG_NR22 = 0X17,    // Sound channel 2 volume & envelope
    REG_NR23 = 0X18,    // Sound channel 2 period low
    REG_NR24 = 0X19,    // Sound channel 2 period high & control
    REG_NR30 = 0X1A,    // Sound channel 3 DAC enable
    REG_NR31 = 0X1B,    // Sound channel 3 length timer
    REG_NR32 = 0X1C,    // Sound channel 3 output level
    REG_NR33 = 0X1D,    // Sound channel 3 period low
    REG_NR34 = 0X1E,    // Sound channel 3 period high & control
    REG_NR41 = 0X20,    // Sound channel 4 length timer
    REG_NR42 = 0X21,    // Sound channel 4 volume & envelope
    REG_NR43 = 0X22,    // Sound channel 4 frequency & randomness
    REG_NR44 = 0X23,    // Sound channel 4 control
    REG_NR50 = 0X24,    // Master volume & VIN panning
    REG_NR51 = 0X25,    // Sound panning
    REG_NR52 = 0X26,    // Sound on/off
    REG_WAVERAM = 0X30, // Storage for one of the sound channels’ waveform
    REG_LCDC = 0X40,    // LCD control
    REG_STAT = 0X41,    // LCD status
    REG_SCY = 0X42,     // Viewport Y position
    REG_SCX = 0X43,     // Viewport X position
    REG_LY = 0X44,      // LCD Y coordinate
    REG_LYC = 0X45,     // LY compare
    REG_DMA = 0X46,     // OAM DMA source address & start
    REG_BGP = 0X47,     // BG palette data
    REG_OBP0 = 0X48,    // OBJ palette 0 data
    REG_OBP1 = 0X49,    // OBJ palette 1 data
    REG_WY = 0X4A,      // Window Y position
    REG_WX = 0X4B,      // Window X position plus 7
    REG_KEY1 = 0X4D,    // Prepare speed switch
    REG_VBK = 0X4F,     // VRAM bank
    REG_HDMA1 = 0X51,   // VRAM DMA source high
    REG_HDMA2 = 0X52,   // VRAM DMA source low
    REG_HDMA3 = 0X53,   // VRAM DMA destination high
    REG_HDMA4 = 0X54,   // VRAM DMA destination low
    REG_HDMA5 = 0X55,   // VRAM DMA length/mode/start	
    REG_RP = 0X56,      // Infrared communications port
    REG_BGPI = 0X68,    // Background color palette specification / Background palette index
    REG_BGPD = 0X69,    // Background color palette data / Background palette data
    REG_OBPI = 0X6A,    // OBJ color palette specification / OBJ palette index
    REG_OBPD = 0X6B,    // OBJ color palette data / OBJ palette data
    REG_OPRI = 0X6C,    // Object priority mode
    REG_SVBK = 0X70,    // WRAM bank
    REG_PCM12 = 0X76,   // Audio digital outputs 1 & 2
    REG_PCM34 = 0X77,   // Audio digital outputs 3 & 4
    REG_IE = 0XFF,      // Interrupt enable
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
enum InterruptBit {
    INT_BIT_VBLANK,
    INT_BIT_STAT,
    INT_BIT_TIMER,
    INT_BIT_SERIAL,
    INT_BIT_JOYPAD
};
enum InterruptVector {
    INT_VEC_VBLANK = 0x40,
    INT_VEC_STAT = 0x48,
    INT_VEC_TIMER = 0x50,
    INT_VEC_SERIAL = 0x58,
    INT_VEC_JOYPAD = 0x60
};
enum LCDControl {           // Bits 0-7 of the LCDC register
    LCDC_BGW_ENABLE,        // BG and Window enable (dmg) / priority (cgb)
    LCDC_OBJ_ENABLE,        // whether objects are displayed or not
    LCDC_OBJ_SZ,            // OBJ (Sprite) Size. 0: 8x8, 1: 8x16 (w*h)
    LCDC_BG_TILEMAP_AREA,   // BG Tile Map Display Select. 0: 0x9800-0x9BFF | 1: 0x9C00-0x9FFF
    LCDC_BGW_TILEDATA_AREA, // BG & Window Tile Data Select. 0: 0x8800-0x97FF | 1: 0x8000-0x8FFF <- Same area as OBJ
    LCDC_W_ENABLE,          // controls whether the window shall be displayed or not. overridden on DMG by bit 0 if that bit is clear
    LCDC_W_TILEMAP_AREA,    // Window tile map display select. 0: 0x9800-0x9BFF | 1: 0x9C00-0x9FFF
    LCDC_ENABLE             // controls whether the LCD is on and the PPU is active. Setting it to 0 turns both off, which grants immediate and full access to VRAM, OAM, etc.
};
enum LCDMode {              // Bits 0-1 of the STAT register
    LCD_MODE_HBLANK,        // The LCD is in the H-Blank period
    LCD_MODE_VBLANK,        // The LCD is in the V-Blank period
    LCD_MODE_OAM,           // The LCD is reading data from Object Attribute Memory (OAM).
    LCD_MODE_VRAM,          // The LCD is reading data from VRAM and sending it to the LCD driver.
};
enum STATIntSource {        // bits 3-6 of the STAT register, indicates whether a STAT interrupt is requested when the condition is met
    STAT_INT_HBLANK = 3,
    STAT_INT_VBLANK = 4,
    STAT_INT_OAM = 5,
    STAT_INT_LYC = 6
};

#define BANKSIZE_ROM    0x4000
#define BANKSIZE_ERAM   0x2000
#define BANKSIZE_VRAM   0x2000
#define BANKSIZE_WRAM   0x1000

/*
u8 boot_rom[] = { // Open-source boot rom
    0x31, 0xFE, 0xFF, 0x21, 0xFF, 0x9F, 0xAF, 0x32, 0xCB, 0x7C, 0x20, 0xFA, 0x0E, 0x11, 0x21, 0x26,
    0xFF, 0x3E, 0x80, 0x32, 0xE2, 0x0C, 0x3E, 0xF3, 0x32, 0xE2, 0x0C, 0x3E, 0x77, 0x32, 0xE2, 0x11,
    0x04, 0x01, 0x21, 0x10, 0x80, 0x1A, 0xCD, 0xB8, 0x00, 0x1A, 0xCB, 0x37, 0xCD, 0xB8, 0x00, 0x13,
    0x7B, 0xFE, 0x34, 0x20, 0xF0, 0x11, 0xCC, 0x00, 0x06, 0x08, 0x1A, 0x13, 0x22, 0x23, 0x05, 0x20,
    0xF9, 0x21, 0x04, 0x99, 0x01, 0x0C, 0x01, 0xCD, 0xB1, 0x00, 0x3E, 0x19, 0x77, 0x21, 0x24, 0x99,
    0x0E, 0x0C, 0xCD, 0xB1, 0x00, 0x3E, 0x91, 0xE0, 0x40, 0x06, 0x10, 0x11, 0xD4, 0x00, 0x78, 0xE0,
    0x43, 0x05, 0x7B, 0xFE, 0xD8, 0x28, 0x04, 0x1A, 0xE0, 0x47, 0x13, 0x0E, 0x1C, 0xCD, 0xA7, 0x00,
    0xAF, 0x90, 0xE0, 0x43, 0x05, 0x0E, 0x1C, 0xCD, 0xA7, 0x00, 0xAF, 0xB0, 0x20, 0xE0, 0xE0, 0x43,
    0x3E, 0x83, 0xCD, 0x9F, 0x00, 0x0E, 0x27, 0xCD, 0xA7, 0x00, 0x3E, 0xC1, 0xCD, 0x9F, 0x00, 0x11,
    0x8A, 0x01, 0xF0, 0x44, 0xFE, 0x90, 0x20, 0xFA, 0x1B, 0x7A, 0xB3, 0x20, 0xF5, 0x18, 0x49, 0x0E,
    0x13, 0xE2, 0x0C, 0x3E, 0x87, 0xE2, 0xC9, 0xF0, 0x44, 0xFE, 0x90, 0x20, 0xFA, 0x0D, 0x20, 0xF7,
    0xC9, 0x78, 0x22, 0x04, 0x0D, 0x20, 0xFA, 0xC9, 0x47, 0x0E, 0x04, 0xAF, 0xC5, 0xCB, 0x10, 0x17,
    0xC1, 0xCB, 0x10, 0x17, 0x0D, 0x20, 0xF5, 0x22, 0x23, 0x22, 0x23, 0xC9, 0x3C, 0x42, 0xB9, 0xA5,
    0xB9, 0xA5, 0x42, 0x3C, 0x00, 0x54, 0xA8, 0xFC, 0x42, 0x4F, 0x4F, 0x54, 0x49, 0x58, 0x2E, 0x44,
    0x4D, 0x47, 0x20, 0x76, 0x31, 0x2E, 0x32, 0x00, 0x3E, 0xFF, 0xC6, 0x01, 0x0B, 0x1E, 0xD8, 0x21,
    0x4D, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3E, 0x01, 0xE0, 0x50
};
*/

// Declare shared memory between the CPU and PPU
extern u8  reg[0x100];     // Refers to Register enum
extern u8  vram[2 * BANKSIZE_VRAM];
extern u8  oam[0xA0];

extern u8  cpu_mode;       // vblank/hblank/oam search/pixel rendering...
extern u8  interrupts_enabled; // IME flag

#endif EMU_SHARED_H