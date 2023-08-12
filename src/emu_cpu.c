#include "emu_cpu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alu_binary.h"
#include "macros.h"

/// <summary>
/// Emulates the Game Boy's DMG/CGB-CPU 
/// </summary>

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
    MEM_ROM_0       = 0x0000, // [0000~3FFF] 16 KiB ROM bank 00 (From cartridge, usually a fixed bank)
    MEM_ROM_N       = 0x4000, // [4000~7FFF] 16 KiB ROM Bank 01~NN (From cartridge, switchable bank via mapper (if any))
    MEM_VRAM        = 0x8000, // [8000~9FFF] 8 KiB Video RAM (VRAM) (In CGB mode, switchable bank 0/1)
    MEM_ERAM        = 0xA000, // [A000~BFFF] 8 KiB External RAM (From cartridge, switchable bank if any)
    MEM_WRAM        = 0xC000, // [C000~CFFF] 4 KiB Work RAM (WRAM) (Volatile - for temporary storage)
    MEM_WRAM_N      = 0xD000, // [D000~DFFF] Used together with WRAM_0 as a contiguous block. In CGB mode, switchable bank 1~7.
    MEM_ECHORAM     = 0xE000, // [E000~FDFF] Mirror of WRAM C000~DDFF (ECHO RAM) (use of this area is prohibited)
    MEM_OAM         = 0xFE00, // [FE00~FE9F] Sprite information for displaying on screen
    MEM_UNUSABLE    = 0xFEA0, // [FEA0~FEFF] Use of this area is prohibited
    MEM_IO          = 0xFF00, // [FF00~FF7F] I/O Registers
    MEM_HRAM        = 0xFF80, // [FF80~FFFE] 127 Bytes High RAM (HRAM)
    MEM_IE          = 0xFFFF, // [FFFF]      Interrupt Enable register (IE)
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
    REG_P1      = 0x00, // Joypad
    REG_SB      = 0X01, // Serial transfer data
    REG_SC      = 0X02, // Serial transfer control
    REG_DIV     = 0X04, // Divider register
    REG_TIMA    = 0X05, // Timer counter
    REG_TMA     = 0X06, // Timer modulo
    REG_TAC     = 0X07, // Timer control
    REG_IF      = 0X0F, // Interrupt flag
    REG_NR10    = 0X10, // Sound channel 1 sweep	
    REG_NR11    = 0X11, // Sound channel 1 length timer & duty cycle
    REG_NR12    = 0X12, // Sound channel 1 volume & envelope
    REG_NR13    = 0X13, // Sound channel 1 period low
    REG_NR14    = 0X14, // Sound channel 1 period high & control
    REG_NR21    = 0X16, // Sound channel 2 length timer & duty cycle
    REG_NR22    = 0X17, // Sound channel 2 volume & envelope
    REG_NR23    = 0X18, // Sound channel 2 period low
    REG_NR24    = 0X19, // Sound channel 2 period high & control
    REG_NR30    = 0X1A, // Sound channel 3 DAC enable
    REG_NR31    = 0X1B, // Sound channel 3 length timer
    REG_NR32    = 0X1C, // Sound channel 3 output level
    REG_NR33    = 0X1D, // Sound channel 3 period low
    REG_NR34    = 0X1E, // Sound channel 3 period high & control
    REG_NR41    = 0X20, // Sound channel 4 length timer
    REG_NR42    = 0X21, // Sound channel 4 volume & envelope
    REG_NR43    = 0X22, // Sound channel 4 frequency & randomness
    REG_NR44    = 0X23, // Sound channel 4 control
    REG_NR50    = 0X24, // Master volume & VIN panning
    REG_NR51    = 0X25, // Sound panning
    REG_NR52    = 0X26, // Sound on/off
    REG_WAVERAM = 0X30, // Storage for one of the sound channels’ waveform
    REG_LCDC    = 0X40, // LCD control
    REG_STAT    = 0X41, // LCD status
    REG_SCY     = 0X42, // Viewport Y position
    REG_SCX     = 0X43, // Viewport X position
    REG_LY      = 0X44, // LCD Y coordinate
    REG_LYC     = 0X45, // LY compare
    REG_DMA     = 0X46, // OAM DMA source address & start
    REG_BGP     = 0X47, // BG palette data
    REG_OBP0    = 0X48, // OBJ palette 0 data
    REG_OBP1    = 0X49, // OBJ palette 1 data
    REG_WY      = 0X4A, // Window Y position
    REG_WX      = 0X4B, // Window X position plus 7
    REG_KEY1    = 0X4D, // Prepare speed switch
    REG_VBK     = 0X4F, // VRAM bank
    REG_HDMA1   = 0X51, // VRAM DMA source high
    REG_HDMA2   = 0X52, // VRAM DMA source low
    REG_HDMA3   = 0X53, // VRAM DMA destination high
    REG_HDMA4   = 0X54, // VRAM DMA destination low
    REG_HDMA5   = 0X55, // VRAM DMA length/mode/start	
    REG_RP      = 0X56, // Infrared communications port
    REG_BGPI    = 0X68, // Background color palette specification / Background palette index
    REG_BGPD    = 0X69, // Background color palette data / Background palette data
    REG_OBPI    = 0X6A, // OBJ color palette specification / OBJ palette index
    REG_OBPD    = 0X6B, // OBJ color palette data / OBJ palette data
    REG_OPRI    = 0X6C, // Object priority mode
    REG_SVBK    = 0X70, // WRAM bank
    REG_PCM12   = 0X76, // Audio digital outputs 1 & 2
    REG_PCM34   = 0X77, // Audio digital outputs 3 & 4
    REG_IE      = 0XFF, // Interrupt enable
};
enum RomHeader {
    ROM_ENTRY           = 0x100, // starting point after boot ROM
    ROM_LOGO            = 0x104, // bitmap image that has to match the boot ROM's
    ROM_TITLE           = 0x134, // ASCII title of the game
    ROM_MANUFACTURER    = 0x13F, // 4-character manufacturer code
    ROM_CGB_FLAG        = 0x143, // whether or not to enable CGB mode (0x80: monochrome+CGB, 0xC0: CGB only)
    ROM_LICENSEE_NEW    = 0x144, // if old license is 0x33, uses this one instead
    ROM_SGB_FLAG        = 0x146, // SuperGameBoy(SNES) mode: if 0x03, ignore any command packets
    ROM_CART_TYPE       = 0x147, // what hardware is present
    ROM_ROM_SIZE        = 0x148, // how much ROM is present (32 KiB ª (1 << <value>))
    ROM_RAM_SIZE        = 0X149, // how much RAM is present, if any. Ignored if CART_TYPE has no RAM
    ROM_DESTINATION     = 0x14A, // 0x00: Japan, 0x0: Global
    ROM_LICENSEE_OLD    = 0x14B, // the SGB will ignore any command packets unless this value is 0x33
    ROM_VERSION         = 0X14C, // version number of the game
    ROM_HEADER_CHECKSUM = 0x14D, // 8-bit checksum that gets verified by the boot ROM
    ROM_GLOBAL_CHECKSUM = 0x14E, // 16-bit checksum (not verified)
};
enum RTCRegister {
    RTC_S   = 0x08, // Seconds (0x00-0x3B)
    RTC_M   = 0x09, // Minutes (0x00-0x3B)
    RTC_H   = 0x0A, // Hours (0x00-0x17)
    RTC_DL  = 0x0B, // Lower 8 bits of Day Counter (0x00-0xFF)
    RTC_DH  = 0x0C, // b0-Upper 1 bit of Day Counter, b6-Halt Flag, b7-Carry Bit
};
#define BANKSIZE_ROM    0x4000
#define BANKSIZE_ERAM   0x2000
#define BANKSIZE_VRAM   0x2000
#define BANKSIZE_WRAM   0x1000



// Hardware registers
BytePair AF; // Accumulator & Flags
BytePair BC;
BytePair DE;
BytePair HL;
u16 SP; // Stack Pointer
u16 PC; // Program Counter/Pointer

// Memory-mapped registers
u8 reg[0x100];  // Refers to Register enum
u8 rtc[0xD];    // Refers to RTCRegister enum

// Memory banks
u8* rom;    // loaded from .gb / .gbc
u8* eram;   // external ram (cartridge)
u8 vram[2 * BANKSIZE_VRAM];
u8 wram[8 * BANKSIZE_ROM];
u8 oam[0xA0];
u8 hram[0x80];

u16 rom_banks;  // up to 512 banks of 16 KB each (8MB)
u8 eram_banks;  // up to 16 banks of 8 KB each (128 KB)
u8 mbc_mode;    // For mbc1 only - 0: 2MiB ROM/8KiB RAM | 1: 512KiB ROM/4*8Kib RAM

u16 rom_bank;   // current bank
u8 rom_bank_2;  // secondary ROM banking register

u8 eram_bank;
//u8 vram_bank;
//u8 wram_bank;

unsigned char title[17]; // 16 + '\0'
unsigned char licensee_code_new[2];
u8 licensee_code_old;
u8 destination_code;

u8 cgb_flag;
u8 sgb_flag;

u8 cart_type;
u8 rom_version;
u8 checksum_header;
u16 checksum_global;

u8 mbc;             // 0: ROM only | 1: MBC1 | 2: MBC2 | 3: MBC3 | 4: MMM01 | 5: MBC5 
u8 eram_enabled;    // RAM/RTC Enable mbc register
u8 rtc_select_reg;  // Indicated which RTC register is currently mapped into memory at A000 - BFFF
                    // any read/write in that range will instead access this register.
u8 rtc_latch_flag;
u8 rtc_latch_reg;

u8 rom_size_code;
u8 eram_size_code;


int emu_cpu_init(u8* rom_buffer)
{
    // Lookup tables for cart type
    u8 mbc_lut[] = {
        0, 1, 1, 1, 0, 2, 2, 0, 0, 0, 0, 4, 4, 4, 0, 3,
        3, 3, 3, 3, 0, 0, 0, 0, 0, 5, 5, 5, 5, 5, 5
    };
    u8 mbc_lut_size = 31;

    rom = rom_buffer;
    // Get ROM header data

    // Title
    memcpy(title, &rom[ROM_TITLE], sizeof(unsigned char) * 16);
    title[16] = '\0'; // adds a string terminator
    printf("Title: %s\n", title);

    // CGB Indicator
    cgb_flag = rom[ROM_CGB_FLAG] == 0x80;
    printf("CGB: %s\n", cgb_flag ? "true" : "false");

    // SGB Indicator
    sgb_flag = rom[ROM_SGB_FLAG] == 0x03;
    printf("SGB: %s\n", sgb_flag ? "true" : "false");

    // Cart type
    cart_type = rom[ROM_CART_TYPE];
    // MBC type
    mbc = (cart_type < mbc_lut_size) ? mbc = mbc_lut[cart_type] : 0;
    // ROM/ERAM banks
    rom_size_code = rom[ROM_ROM_SIZE];
    eram_size_code = rom[ROM_RAM_SIZE];

    switch (rom_size_code) {
        case 0x0:   rom_banks = 2;   break; // 32  kB
        case 0x1:   rom_banks = 4;   break; // 64  kB
        case 0x2:   rom_banks = 8;   break; // 128 kB
        case 0x3:   rom_banks = 16;  break; // 256 kB
        case 0x4:   rom_banks = 32;  break; // 512 kB
        case 0x5:   rom_banks = 64;  break; // 1   MB
        case 0x6:   rom_banks = 128; break; // 2   MB
        case 0x7:   rom_banks = 256; break; // 4   MB
        case 0x52:  rom_banks = 72;  break; // 1.1 MB
        case 0x53:  rom_banks = 80;  break; // 1.2 MB
        case 0x54:  rom_banks = 96;  break; // 1.5 MB
        default:    rom_banks = 2;   break;
    }
    switch (eram_size_code) {
        case 0x0:   eram_banks = 0;  break; // none
        case 0x1:   eram_banks = 1;  break; // 2   kB
        case 0x2:   eram_banks = 1;  break; // 8   kB
        case 0x3:   eram_banks = 4;  break; // 32  kB
        case 0x4:   eram_banks = 16; break; // 128 kB
        default:    eram_banks = 0;  break;
    }

    // Licensee code
    memcpy(title, &rom[ROM_LICENSEE_NEW], sizeof(unsigned char) * 2); // Stored as 2 char ascii
    printf("Licensee new: %c%c\n", licensee_code_new[0], licensee_code_new[1]);
    licensee_code_old = rom[ROM_LICENSEE_OLD];
    printf("Licensee old: %02X\n", licensee_code_old);
    // Misc
    destination_code    = rom[ROM_DESTINATION];
    rom_version         = rom[ROM_VERSION];
    checksum_header     = rom[ROM_HEADER_CHECKSUM];
    checksum_global     = rom[ROM_GLOBAL_CHECKSUM] << 8 || rom[ROM_GLOBAL_CHECKSUM + 1];

    
    printf("Cart type: %d\nMBC: %d\nROM banks: %d\nERAM banks: %d\n", cart_type, mbc, rom_banks, eram_banks);

    //AF.high = GET_BIT(AF.high, 3);
    //RESET_BIT(AF.high, 3);
    //printf("AF: %04X\n", AF.full);
    //SET_BIT(AF.high, 3);
    //printf("AF: %04X\n", AF.full);

    return 0;
}

int power_up()
{
    // Reset registers to their default values
    AF.high         = 0x11;
    AF.low          = 0xB0;
    BC.full         = 0x0013;
    DE.full         = 0x00D8;
    HL.full         = 0x014D;
    PC              = 0x0100;
    SP              = 0xFFFE;

    mbc_mode        = 0;
    rom_bank        = 0;
    rom_bank_2      = 0;
    
    reg[REG_P1]     = 0xC7;
    reg[REG_SB]     = 0x00;
    reg[REG_SC]     = 0x7F;
    reg[REG_DIV]    = 0x18;
    reg[REG_TIMA]   = 0x00;
    reg[REG_TMA]    = 0x00;
    reg[REG_TAC]    = 0xF8;
    reg[REG_IF]     = 0xE1;
    
    reg[REG_NR10]   = 0x80;
    reg[REG_NR11]   = 0xBF;
    reg[REG_NR12]   = 0xF3;
    reg[REG_NR13]   = 0xFF;
    reg[REG_NR14]   = 0xBF;

    reg[REG_NR21]   = 0x3F;
    reg[REG_NR22]   = 0x00;
    reg[REG_NR23]   = 0xFF;
    reg[REG_NR24]   = 0xBF;

    reg[REG_NR30]   = 0x7F;
    reg[REG_NR31]   = 0xFF;
    reg[REG_NR32]   = 0x9F;
    reg[REG_NR33]   = 0xFF;
    reg[REG_NR34]   = 0xBF;

    reg[REG_NR41]   = 0xFF;
    reg[REG_NR42]   = 0x00;
    reg[REG_NR43]   = 0x00;
    reg[REG_NR44]   = 0xBF;

    reg[REG_NR50]   = 0x77;
    reg[REG_NR51]   = 0xF3;
    reg[REG_NR52]   = 0xF1; // F1-GB, F0-SGB

    reg[REG_LCDC]   = 0x91;
    reg[REG_STAT]   = 0x81;
    reg[REG_SCY]    = 0x00;
    reg[REG_SCX]    = 0x00;
    reg[REG_LY]     = 0x91;
    reg[REG_LYC]    = 0x00;

    reg[REG_DMA]    = 0x00;
    reg[REG_BGP]    = 0xFC;
    reg[REG_OBP0]   = 0xFF;
    reg[REG_OBP1]   = 0xFF;

    reg[REG_WY]     = 0x00;
    reg[REG_WX]     = 0x00;

    reg[REG_KEY1]   = 0xFF;

    reg[REG_VBK]    = 0xFF;
    reg[REG_HDMA1]  = 0xFF;
    reg[REG_HDMA2]  = 0xFF;
    reg[REG_HDMA3]  = 0xFF;
    reg[REG_HDMA4]  = 0xFF;
    reg[REG_HDMA5]  = 0xFF;

    reg[REG_RP]     = 0xFF;

    reg[REG_BGPI]   = 0xFF;
    reg[REG_BGPD]   = 0xFF;
    reg[REG_OBPI]   = 0xFF;
    reg[REG_OBPD]   = 0xFF;

    reg[REG_SVBK]   = 0xFF;

    reg[REG_IE]     = 0x00;

    return 0;
}


u8 read(u16 addr)
{
    switch ((u8)(addr << 12))
    {

    }
}

int write(u16 addr, u8 value)
{
    u8 msb = (u8)(addr >> 12);
    // MBC Registers
    if (msb < 0x8) {
        switch (mbc) {
            case 1:
            {
                switch (msb) {
                    case 0x0:
                    case 0x1:
                        // 4 bit register - RAM Enabled
                        eram_enabled = ((value & 0xF) == 0xA);
                        break;
                    case 0x2:
                    case 0x3:
                    {
                        // 5 bit register - ROM bank number
                        u8 rb = (value & 0x1F);
                        if (rb < rom_banks) {
                            rom_bank = (rb == 0) ? 1 : rb; // 0 behaves as 1
                        }
                        else {
                            // Mask the number to the max banks
                            rom_bank = (rb % rom_banks);
                        }
                        // On larger carts which need a >5 bit bank number, 
                        // the secondary banking register is used to supply an additional 2 bits for the effective bank number
                        if (rom_banks > 0x1F) {
                            rom_bank = rom_bank + (rom_bank_2 << 5);
                            // MMM01 (multi-cart) has a different formula
                            // These additional two bits are ignored for the bank 00 -> 01 translation
                        }
                    } break;
                    case 0x4:
                    case 0x5:
                    {
                        // 2 bit register - RAM bank number / Upper bits of ROM bank number
                        if (eram_banks >= 4)        eram_bank = (value & 0x3);
                        else if (rom_banks >= 64)   rom_bank_2 = (value & 0x3);
                        // In MMM01 this 2-bit register is instead applied to bits 4-5 
                        // of the ROM bank number and the top bit of the main 5-bit main ROM banking register is ignored
                    } break;
                    case 0x6:
                    case 0x7:
                    {
                        // 1 bit register - Banking mode select
                        // 00 = Simple Banking Mode (default) 
                        // 01 = RAM Banking Mode / Advanced ROM Banking Mode
                        if ((eram_size_code >= 2) && (rom_banks >= 32)) {
                            mbc_mode = (value & 1);
                        }
                    } break;
                }
            } break;
            case 2:
            {
                switch (msb) {
                    case 0x0:
                    case 0x1:
                    case 0x2:
                    case 0x3:
                    {
                        // RAM enable  / ROM bank number
                        if (((addr >> 8) & 1) == 0) {
                            eram_enabled = (value == 0xA);
                        }
                        else {
                            rom_bank = (value & 0xF);
                            if (rom_bank == 0) rom_bank = 1;
                        }
                    } break;
                }
            } break;
            case 3:
            {
                switch ((u8)(addr >> 12)) {
                    case 0x0:
                    case 0x1:
                    {
                        // RAM and Timer enable
                        eram_enabled = ((value & 0xF) == 0xA);
                    } break;
                    case 0x2:
                    case 0x3:
                    {
                        // 7 bit register - ROM bank number
                        u8 rb = (value & 0x7F);
                        if (rb < rom_banks) {
                            rom_bank = (rb == 0) ? 1 : rb; // 0 behaves as 1
                        }
                        else {
                            // Mask the number to the max banks
                            rom_bank = (rb % rom_banks);
                        }
                    } break;
                    case 0x4:
                    case 0x5:
                    {
                        // RAM bank number / RTC register select
                        if (value <= 0x03) {
                            if (eram_banks >= 4) eram_bank = (value & 0x3);
                            rtc_select_reg = 0;
                        }
                        else if (value >= 0x08 && value <= 0x0C) {
                            rtc_select_reg = value;
                        }
                    } break;
                    case 0x6:
                    case 0x7:
                    {
                        // Latch Clock Data
                        // When writing 0x00, and then 0x01 to this register, 
                        // the current time becomes latched into the RTC registers. 
                        // The latched data will not change until it becomes latched again, by repeating the procedure.
                        if ((rtc_latch_reg == value) && (value <= 0x01)) {
                            rtc_latch_reg++;
                            if (rtc_latch_reg == 2) {
                                rtc_latch_reg = 0;
                                rtc_latch_flag = !rtc_latch_flag;
                                // TODO: Latch the current time into the rtc registers...
                                if (rtc_latch_flag) {

                                }
                            }
                        }
                    } break;
                }
            } break;
            case 5:
            {
                switch ((u8)(addr >> 12)) {
                    case 0x0:
                    case 0x1:
                    {
                        // RAM enable
                        eram_enabled = ((value & 0xF) == 0xA);
                    } break;
                    case 0x2:
                    {
                        // 8 bit register - ROM bank number
                        if (value < rom_banks) {
                            rom_bank = value;
                        }
                        else {
                            // Mask the number to the max banks
                            rom_bank = (value % rom_banks);
                        }
                        // On larger carts which need a >8 bit bank number, 
                        // the secondary banking register is used to supply an additional 1 bits for the effective bank number
                        if (rom_banks > 0xFF) {
                            rom_bank = (rom_bank + (rom_bank_2 << 8));
                        }
                    } break;
                    case 0x3:
                    {
                        // 9th bit of ROM bank number
                        rom_bank_2 = (value & 1);
                    } break;
                    case 0x4:
                    case 0x5:
                    {
                        // RAM bank number
                        if (eram_banks >= value & 0xF) eram_bank = (value & 0xF);
                    } break;
                }
            } break;
        }
    }
    else {
        switch (msb) {
            case 0x8:
            case 0x9:
                // VRAM
                if (cgb_flag)   vram[(addr & 0x1FF) + (reg[REG_VBK] * BANKSIZE_VRAM)] = value;
                else            vram[addr & 0x1FF] = value;
                break;
            case 0xA:
            case 0xB:
                // ERAM
                if (!eram_enabled) return -1;
                if (mbc == 2) {
                    // Half bytes, Bottom 9 bits of address are used to index RAM
                    eram[addr & 0x1FF] = (value & 0xF);
                }
                else if (mbc == 3 && rtc_select_reg > 0) {
                    // RTC register write
                    rtc[rtc_select_reg] = value; 
                }
                else {
                    // "& 0x1FF" extracts the lower 13 bits, which maps the address to the array range starting from 0x0
                    eram[(addr & 0x1FFF) + (eram_bank * BANKSIZE_ERAM)] = value;
                }
                break;
            case 0xC:
            case 0xE:
                // WRAM / ECHO RAM
                wram[addr & 0xFFF] = value;
                break;
            case 0xD:
            case 0xF:
                // WRAM Bank / ECHO RAM
                if (addr < MEM_OAM) {
                    if (cgb_flag)   wram[(addr & 0x1FFF) + (reg[REG_SVBK] * BANKSIZE_WRAM)] = value;
                    else            wram[addr & 0x1FFF] = value;
                }
                // Object attribute memory (OAM)
                else if (addr >= MEM_OAM && addr < MEM_UNUSABLE) {
                    oam[addr - MEM_OAM] = value; // Convert to range 0-159
                }
                // I/O Registers
                else if (addr >= MEM_IO && addr < MEM_HRAM) {
                    reg[addr & 0xFF] = value;   // Convert to range 0-255
                }
                // High RAM
                else if (addr >= MEM_HRAM && addr < MEM_IE) {
                    hram[addr & 0x7F] = value;  // Convert to range 0-127
                }
                // Interrupt Enable register (IE)
                else {
                    reg[REG_IE] = value;
                }
        }
    }
    return 0;
}



void emu_cpu_update()
{
    u8 op; // the current operand read from memory at PC location
    u8 cycles;
    int cycles_this_update = 0;
    while (cycles_this_update < MAXDOTS)
    {
        // Fetch instruction
        //OP = read[PC++];
        
        cycles = 4; // instead of 4, a value is returned by the operand instruction
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
    if (rom) free(rom);
    if (eram) free(eram);
}
