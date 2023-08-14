/// <summary>
/// Emulates the Game Boy's DMG/CGB-CPU 
/// </summary>

#include "emu_cpu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alu_binary.h"
#include "emu_cpu_helper.h"
#include "macros.h"


// Hardware registers
u8 A;   // Accumulator
u8 F_Z; // Zero flag
u8 F_N; // Subtract flag
u8 F_H; // Half carry flag
u8 F_C; // Carry flag 
BytePair BC;
BytePair DE;
BytePair HL;
BytePair SP; // Stack Pointer
u16 PC; // Program Counter/Pointer

u8 prefix_cb;

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

// Header information
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
u8 rom_size_code;
u8 eram_size_code;
u8 mbc;             // 0: ROM only | 1: MBC1 | 2: MBC2 | 3: MBC3 | 4: MMM01 | 5: MBC5 

// MBC Registers
u16 rom_bank;       // current bank
u8 rom_bank_2;      // secondary ROM banking register
u8 eram_bank;
u8 eram_enabled;    // RAM/RTC Enable mbc register
u8 mbc_mode;        // For mbc1 only - 0: 2MiB ROM/8KiB RAM | 1: 512KiB ROM/4*8Kib RAM
u8 rtc_latch_flag;
u8 rtc_latch_reg;
u8 rtc_select_reg;  // Indicated which RTC register is currently mapped into memory at A000 - BFFF

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
void add_u8(u8* a, u8 b) {
    F_H = HALF_CARRY_U8_ADD(*a, b);
    F_C = CARRY_ADD(*a, b);
    F_N = 0;
    (*a) += b;
    F_Z = (*a == 0);
}
void add_u16(u16* a, u16 b) {
    F_H = HALF_CARRY_U16_ADD(*a, b);
    F_C = CARRY_ADD(*a, b);
    F_N = 0;
    (*a) += b;
}
void sub_u8(u8 b) {
    F_H = HALF_CARRY_U8_SUB(A, b);
    F_C = CARRY_ADD(A, b);
    F_N = 1;
    A -= b;
    F_Z = (A == 0);
}
void cp_u8(u8 b) {
    // Same as sub, but discards the results and only updates the flags
    F_H = HALF_CARRY_U8_SUB(A, b);
    F_C = CARRY_ADD(A, b);
    F_N = 1;
    F_Z = (A - b == 0);
}
void and_u8(u8 b) {
    F_H = 1;
    F_C = 0;
    F_N = 0;
    A &= b;
    F_Z = (A == 0);
}
void xor_u8(u8 b) {
    F_H = 0;
    F_C = 0;
    F_N = 0;
    A ^= b;
    F_Z = (A == 0);
}
void or_u8(u8 b) {
    F_H = 0;
    F_C = 0;
    F_N = 0;
    A |= b;
    F_Z = (A == 0);
}

int power_up()
{
    // Reset registers to their default values
    A = 0x11;
    //AF.low          = 0xB0;
    F_Z = 1;
    F_N = 0;
    F_H = 1;
    F_C = 1;

    BC.full = 0x0013;
    DE.full = 0x00D8;
    HL.full = 0x014D;
    SP.full = 0xFFFE;
    PC = 0x0100;

    prefix_cb = 0;

    mbc_mode = 0;
    rom_bank = 1;
    rom_bank_2 = 0;
    eram_enabled = 0;

    reg[REG_P1] = 0xC7;
    reg[REG_SB] = 0x00;
    reg[REG_SC] = 0x7F;
    reg[REG_DIV] = 0x18;
    reg[REG_TIMA] = 0x00;
    reg[REG_TMA] = 0x00;
    reg[REG_TAC] = 0xF8;
    reg[REG_IF] = 0xE1;

    reg[REG_NR10] = 0x80;
    reg[REG_NR11] = 0xBF;
    reg[REG_NR12] = 0xF3;
    reg[REG_NR13] = 0xFF;
    reg[REG_NR14] = 0xBF;

    reg[REG_NR21] = 0x3F;
    reg[REG_NR22] = 0x00;
    reg[REG_NR23] = 0xFF;
    reg[REG_NR24] = 0xBF;

    reg[REG_NR30] = 0x7F;
    reg[REG_NR31] = 0xFF;
    reg[REG_NR32] = 0x9F;
    reg[REG_NR33] = 0xFF;
    reg[REG_NR34] = 0xBF;

    reg[REG_NR41] = 0xFF;
    reg[REG_NR42] = 0x00;
    reg[REG_NR43] = 0x00;
    reg[REG_NR44] = 0xBF;

    reg[REG_NR50] = 0x77;
    reg[REG_NR51] = 0xF3;
    reg[REG_NR52] = 0xF1; // F1-GB, F0-SGB

    reg[REG_LCDC] = 0x91;
    reg[REG_STAT] = 0x81;
    reg[REG_SCY] = 0x00;
    reg[REG_SCX] = 0x00;
    reg[REG_LY] = 0x91;
    reg[REG_LYC] = 0x00;

    reg[REG_DMA] = 0x00;
    reg[REG_BGP] = 0xFC;
    reg[REG_OBP0] = 0xFF;
    reg[REG_OBP1] = 0xFF;

    reg[REG_WY] = 0x00;
    reg[REG_WX] = 0x00;

    reg[REG_KEY1] = 0xFF;

    reg[REG_VBK] = 0xFF;
    reg[REG_HDMA1] = 0xFF;
    reg[REG_HDMA2] = 0xFF;
    reg[REG_HDMA3] = 0xFF;
    reg[REG_HDMA4] = 0xFF;
    reg[REG_HDMA5] = 0xFF;

    reg[REG_RP] = 0xFF;

    reg[REG_BGPI] = 0xFF;
    reg[REG_BGPD] = 0xFF;
    reg[REG_OBPI] = 0xFF;
    reg[REG_OBPD] = 0xFF;

    reg[REG_SVBK] = 0xFF;

    reg[REG_IE] = 0x00;

    return 0;
}

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

    power_up();

    return 0;
}

u8 read(u16 addr)
{
    // TODO - I/O register reading rules

    u8 msb = (u8)(addr >> 12);
    switch (msb) {
        case 0x0:
        case 0x1:
        case 0x2:
        case 0x3:
            // In MBC1 mode-1: ROM bank X0
            if (mbc == 1 && mbc_mode == 1) {
                return rom[rom_bank_2 << 5];
            }
            // ROM bank 0
            else {
                return rom[addr];
            }
        case 0x4:
        case 0x5:
        case 0x6:
        case 0x7:
            // ROM bank > 0
            return rom[(addr & 0x3FFF) + (rom_bank * BANKSIZE_ROM)];
        case 0xA:
        case 0xB:
            // RAM bank 00-03, if any
            if (!eram_enabled) return 0xFF;
            if (mbc == 2) {
                // Half bytes, Bottom 9 bits of address are used to index RAM
                return eram[addr & 0x1FF];
            }
            else if (mbc == 3 && rtc_select_reg > 0) {
                // RTC register read
                return rtc[rtc_select_reg];
            }
            else {
                return eram[(addr & 0x1FFF) + (eram_bank * BANKSIZE_ERAM)];
            }
        case 0xC:
        case 0xE:
            // WRAM / ECHO RAM
            return wram[addr & 0xFFF];
        case 0xD:
        case 0xF:
            // WRAM Bank / ECHO RAM
            if (addr < MEM_OAM) {
                if (cgb_flag)   return wram[(addr & 0x1FFF) + (reg[REG_SVBK] * BANKSIZE_WRAM)];
                else            return wram[addr & 0x1FFF];
            }
            // Object attribute memory (OAM)
            else if (addr >= MEM_OAM && addr < MEM_UNUSABLE) {
                return oam[addr - MEM_OAM]; // Convert to range 0-159
            }
            // I/O Registers
            else if (addr >= MEM_IO && addr < MEM_HRAM) {
                return reg[addr & 0xFF];   // Convert to range 0-255
            }
            // High RAM
            else if (addr >= MEM_HRAM && addr < MEM_IE) {
                return hram[addr & 0x7F];  // Convert to range 0-127
            }
            // Interrupt Enable register (IE)
            else {
                return reg[REG_IE];
            }
    }
}

int write(u16 addr, u8 value)
{
    // TODO - I/O register writing rules

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
                            if ((rom_bank & 0xE0) == 0x00) {
                                // The lower 5 bits of the value are all zero (0x00, 0x20, 0x40, or 0x60))
                                rom_bank += 1;
                            }
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


u8 execute_instruction(u8 op) {
    u8       cycles = op_cycles_lut[op];
    s8       t_s8;
    u8       t_u8;
    BytePair t_u16;

    if (!prefix_cb) {
        switch (op) {  // [Z N H C]
            case 0x00: // NOP
                break;
            case 0x01: // LD BC,d16
                BC.low = read(PC++);
                BC.high = read(PC++);
                break;
            case 0x02: // LD (BC),A
                write(BC.full, A);
                break;
            case 0x03: // INC BC
                BC.full++;
                break;
            case 0x04: // INC B
                inc_u8(&BC.high);
                break;
            case 0x05: // DEC B
                dec_u8(&BC.high);
                break;
            case 0x06: // LD B,d8
                BC.high = read(PC++);
                break;
            case 0x07: // RLCA
                F_C = GET_BIT(A, 7);
                A = ROTATE_LEFT(A, 1, 8);
                F_Z = 0;
                F_N = 0;
                F_H = 0;
                break;
            case 0x08: // LD (a16),SP
                write(PC++, SP.low);
                write(PC++, SP.high);
                break;
            case 0x09: // ADD HL,BC
                add_u16(&HL.full, BC.full);
                break;
            case 0x0A: // LD A,(BC)
                A = read(BC.full);
                break;
            case 0x0B: // DEC BC
                BC.full--;
                break;
            case 0x0C: // INC C
                inc_u8(&BC.low);
                break;
            case 0x0D: // DEC C
                dec_u8(&BC.low);
                break;
            case 0x0E: // LD C,d8
                BC.low = read(PC++);
                break;
            case 0x0F: // RRCA
                F_C = GET_BIT(A, 0);
                A = ROTATE_RIGHT(A, 1, 8);
                F_Z = 0;
                F_N = 0;
                F_H = 0;
                break;
            case 0x10: // STOP 0
                // TODO
                break;
            case 0x11: // LD DE,d16
                DE.low = read(PC++);
                DE.high = read(PC++);
                break;
            case 0x12: // LD (DE),A
                write(DE.full, A);
                break;
            case 0x13: // INC DE
                DE.full++;
                break;
            case 0x14: // INC D
                inc_u8(&DE.high);
                break;
            case 0x15: // DEC D
                dec_u8(&DE.high);
                break;
            case 0x16: // LD D,d8
                DE.high = read(PC++);
                break;
            case 0x17: // RLA
                t_u8 = F_C;
                F_C = GET_BIT(A, 7);
                A <<= 1;
                if (t_u8) SET_BIT(A, 0);
                F_Z = 0;
                F_N = 0;
                F_H = 0;
                break;
            case 0x18: // JR r8
                t_s8 = (s8)read(PC++);
                PC += t_s8;
                break;
            case 0x19: // ADD HL,DE
                add_u16(&HL.full, DE.full);
                break;
            case 0x1A: // LD A,(DE)
                A = read(DE.full);
                break;
            case 0x1B: // DEC DE
                DE.full--;
                break;
            case 0x1C: // INC E
                inc_u8(&DE.low);
                break;
            case 0x1D: // DEC E
                dec_u8(&DE.low);
                break;
            case 0x1E: // LD E,d8
                DE.low = read(PC++);
                break;
            case 0x1F: // RRA
                t_u8 = F_C;
                F_C = GET_BIT(A, 0);
                A >>= 1;
                if (t_u8) SET_BIT(A, 7);
                F_Z = 0;
                F_N = 0;
                F_H = 0;
                break;
            case 0x20: // JR NZ,r8
                t_s8 = (s8)read(PC++);
                if (!F_Z) {
                    PC += t_s8;
                    cycles += 4; // additional cycles if action was taken
                }
                break;
            case 0x21: // LD HL,d16
                HL.low = read(PC++);
                HL.high = read(PC++);
                break;
            case 0x22: // LD (HL+),A
                write(HL.full++, A);
                break;
            case 0x23: // INC HL
                HL.full++;
                break;
            case 0x24: // INC H
                inc_u8(&HL.high);
                break;
            case 0x25: // DEC H
                dec_u8(&HL.high);
                break;
            case 0x26: // LD H,d8
                HL.high = read(PC++);
                break;
            case 0x27: // DAA
                if (F_N == 0) {
                    // after an addition, adjust if (half-)carry occurred or if result is out of bounds
                    if (F_C || A > 0x99) {
                        A += 0x60; F_C = 1;
                    } // upper nibble
                    if (F_H || ((A & 0xF) > 0x9)) {
                        A += 0x6;
                    }  // lower nibble
                }
                else {
                    // after a subtraction, only adjust if (half-)carry occurred
                    if (F_C) A -= 0x60; // upper nibble
                    if (F_H) A -= 0x6;  // lower nibble
                }
                F_Z = (A == 0);
                F_H = 0;
                break;
            case 0x28: // JR Z,r8
                t_s8 = (s8)read(PC++);
                if (F_Z) {
                    PC += t_s8;
                    cycles += 4; // additional cycles if action was taken
                }
                break;
            case 0x29: // ADD HL,HL
                add_u16(&HL.full, HL.full);
                break;
            case 0x2A: // LD A,(HL+)
                A = read(HL.full++);
                break;
            case 0x2B: // DEC HL
                HL.full--;
                break;
            case 0x2C: // INC L
                inc_u8(&HL.low);
                break;
            case 0x2D: // DEC L
                dec_u8(&HL.low);
                break;
            case 0x2E: // LD L,d8
                HL.low = read(PC++);
                break;
            case 0x2F: // CPL
                A ^= 0xFF; // flip bits
                F_N = 1;
                F_H = 1;
                break;
            case 0x30: // JR NC,r8
                t_s8 = (s8)read(PC++);
                if (!F_C) {
                    PC += t_s8;
                    cycles += 4;
                }
                break;
            case 0x31: // LD SP,d16
                SP.low = read(PC++);
                SP.high = read(PC++);
                PC += 2;
                break;
            case 0x32: // LD (HL-),A
                write(HL.full--, A);
                break;
            case 0x33: // INC SP
                SP.full++;
                break;
            case 0x34: // INC (HL)
                t_u8 = read(HL.full);
                inc_u8(&t_u8);
                write(HL.full, t_u8);
                break;
            case 0x35: // DEC (HL)
                t_u8 = read(HL.full);
                dec_u8(&t_u8);
                write(HL.full, t_u8);
                break;
            case 0x36: // LD (HL),d8
                write(HL.full, read(PC++));
                break;
            case 0x37: // SCF
                F_C = 1;
                F_H = 0;
                F_N = 0;
                break;
            case 0x38: // JR C,r8
                t_s8 = (s8)read(PC++);
                if (F_C) {
                    PC += t_s8;
                    cycles += 4; // additional cycles if action was taken
                }
                break;
            case 0x39: // ADD HL,SP
                add_u16(&HL.full, SP.full);
                break;
            case 0x3A: // LD A,(HL-)
                A = read(HL.full--);
                break;
            case 0x3B: // DEC SP
                SP.full--;
                break;
            case 0x3C: // INC A
                inc_u8(&A);
                break;
            case 0x3D: // DEC A
                dec_u8(&A);
                break;
            case 0x3E: // LD A,d8
                A = read(PC++);
                break;
            case 0x3F: // CCF
                F_C ^= F_C;
                F_H = 0;
                F_N = 0;
                break;
            case 0x40: // LD B,B
                // BC.high = BC.high;
                break;
            case 0x41: // LD B,C
                BC.high = BC.low;
                break;
            case 0x42: // LD B,D
                BC.high = DE.high;
                break;
            case 0x43: // LD B,E
                BC.high = DE.low;
                break;
            case 0x44: // LD B,H
                BC.high = HL.high;
                break;
            case 0x45: // LD B,L
                BC.high = HL.low;
                break;
            case 0x46: // LD B,(HL)
                BC.high = read(HL.full);
                break;
            case 0x47: // LD B,A
                BC.high = A;
                break;
            case 0x48: // LD C,B
                BC.low = BC.high;
                break;
            case 0x49: // LD C,C
                // BC.low = BC.low;
                break;
            case 0x4A: // LD C,D
                BC.low = DE.high;
                break;
            case 0x4B: // LD C,E
                BC.low = DE.low;
                break;
            case 0x4C: // LD C,H
                BC.low = HL.high;
                break;
            case 0x4D: // LD C,L
                BC.low = HL.low;
                break;
            case 0x4E: // LD C,(HL)
                BC.low = read(HL.full);
                break;
            case 0x4F: // LD C,A
                BC.low = A;
                break;
            case 0x50: // LD D,B
                DE.high = BC.high;
                break;
            case 0x51: // LD D,C
                DE.high = BC.low;
                break;
            case 0x52: // LD D,D
                // DE.high = DE.high;
                break;
            case 0x53: // LD D,E
                DE.high = DE.low;
                break;
            case 0x54: // LD D,H
                DE.high = HL.high;
                break;
            case 0x55: // LD D,L
                DE.high = HL.low;
                break;
            case 0x56: // LD D,(HL)
                DE.high = read(HL.full);
                break;
            case 0x57: // LD D,A
                DE.high = A;
                break;
            case 0x58: // LD E,B
                DE.low = BC.high;
                break;
            case 0x59: // LD E,C
                DE.low = BC.low;
                break;
            case 0x5A: // LD E,D
                DE.low = DE.high;
                break;
            case 0x5B: // LD E,E
                //DE.low = DE.low;
                break;
            case 0x5C: // LD E,H
                DE.low = HL.high;
                break;
            case 0x5D: // LD E,L
                DE.low = HL.low;
                break;
            case 0x5E: // LD E,(HL)
                DE.low = read(HL.full);
                break;
            case 0x5F: // LD E,A
                DE.low = A;
                break;
            case 0x60: // LD H,B
                HL.high = BC.high;
                break;
            case 0x61: // LD H,C
                HL.high = BC.low;
                break;
            case 0x62: // LD H,D
                HL.high = DE.high;
                break;
            case 0x63: // LD H,E
                HL.high = DE.low;
                break;
            case 0x64: // LD H,H
                //HL.high = HL.high;
                break;
            case 0x65: // LD H,L
                HL.high = HL.low;
                break;
            case 0x66: // LD H,(HL)
                HL.high = read(HL.full);
                break;
            case 0x67: // LD H,A
                HL.high = A;
                break;
            case 0x68: // LD L,B
                HL.low = BC.high;
                break;
            case 0x69: // LD L,C
                HL.low = BC.low;
                break;
            case 0x6A: // LD L,D
                HL.low = DE.high;
                break;
            case 0x6B: // LD L,E
                HL.low = DE.low;
                break;
            case 0x6C: // LD L,H
                HL.low = HL.high;
                break;
            case 0x6D: // LD L,L
                //HL.low = HL.low;
                break;
            case 0x6E: // LD L,(HL)
                HL.low = read(HL.full);
                break;
            case 0x6F: // LD L,A
                HL.low = A;
                break;
            case 0x70: // LD (HL),B
                write(HL.full, BC.high);
                break;
            case 0x71: // LD (HL),C
                write(HL.full, BC.low);
                break;
            case 0x72: // LD (HL),D
                write(HL.full, DE.high);
                break;
            case 0x73: // LD (HL),E
                write(HL.full, DE.low);
                break;
            case 0x74: // LD (HL),H
                write(HL.full, HL.high);
                break;
            case 0x75: // LD (HL),L
                write(HL.full, HL.low);
                break;
            case 0x76: // HALT
                // TODO
                break;
            case 0x77: // LD (HL),A
                write(HL.full, A);
                break;
            case 0x78: // LD A,B
                A = BC.high;
                break;
            case 0x79: // LD A,C
                A = BC.low;
                break;
            case 0x7A: // LD A,D
                A = DE.high;
                break;
            case 0x7B: // LD A,E
                A = DE.low;
                break;
            case 0x7C: // LD A,H
                A = HL.high;
                break;
            case 0x7D: // LD A,L
                A = HL.low;
                break;
            case 0x7E: // LD A,(HL)
                A = read(HL.full);
                break;
            case 0x7F: // LD A,A
                //A = A;
                break;
            case 0x80: // ADD A,B
                add_u8(&A, BC.high);
                break;
            case 0x81: // ADD A,C
                add_u8(&A, BC.low);
                break;
            case 0x82: // ADD A,D
                add_u8(&A, DE.high);
                break;
            case 0x83: // ADD A,E
                add_u8(&A, DE.low);
                break;
            case 0x84: // ADD A,H
                add_u8(&A, HL.high);
                break;
            case 0x85: // ADD A,L
                add_u8(&A, HL.low);
                break;
            case 0x86: // ADD A,(HL)
                add_u8(&A, read(HL.full));
                break;
            case 0x87: // ADD A,A
                add_u8(&A, A);
                break;
            case 0x88: // ADC A,B
                add_u8(&A, BC.high + F_C);
                break;
            case 0x89: // ADC A,C
                add_u8(&A, BC.low + F_C);
                break;
            case 0x8A: // ADC A,D
                add_u8(&A, DE.high + F_C);
                break;
            case 0x8B: // ADC A,E
                add_u8(&A, DE.low + F_C);
                break;
            case 0x8C: // ADC A,H
                add_u8(&A, HL.high + F_C);
                break;
            case 0x8D: // ADC A,L
                add_u8(&A, HL.low + F_C);
                break;
            case 0x8E: // ADC A,(HL)
                add_u8(&A, read(HL.full));
                break;
            case 0x8F: // ADD A,A
                add_u8(&A, A);
                A += F_C;
                break;
            case 0x90: // SUB B
                sub_u8(BC.high);
                break;
            case 0x91: // SUB C
                sub_u8(BC.low);
                break;
            case 0x92: // SUB D
                sub_u8(DE.high);
                break;
            case 0x93: // SUB E
                sub_u8(DE.low);
                break;
            case 0x94: // SUB H
                sub_u8(HL.high);
                break;
            case 0x95: // SUB L
                sub_u8(HL.low);
                break;
            case 0x96: // SUB (HL)
                sub_u8(read(HL.full));
                break;
            case 0x97: // SUB A
                sub_u8(A);
                break;
            case 0x98: // SBC B
                sub_u8(BC.high - F_C);
                break;
            case 0x99: // SBC C
                sub_u8(BC.low - F_C);
                break;
            case 0x9A: // SBC D
                sub_u8(DE.high - F_C);
                break;
            case 0x9B: // SBC E
                sub_u8(DE.low - F_C);
                break;
            case 0x9C: // SBC H
                sub_u8(HL.high - F_C);
                break;
            case 0x9D: // SBC L
                sub_u8(HL.low - F_C);
                break;
            case 0x9E: // SBC (HL)
                sub_u8(read(HL.full) - F_C);
                break;
            case 0x9F: // SBC A
                sub_u8(A - F_C);
                break;
            case 0xA0: // AND B
                and_u8(BC.high);
                break;
            case 0xA1: // AND C
                and_u8(BC.low);
                break;
            case 0xA2: // AND D
                and_u8(DE.high);
                break;
            case 0xA3: // AND E
                and_u8(DE.low);
                break;
            case 0xA4: // AND H
                and_u8(HL.high);
                break;
            case 0xA5: // AND L
                and_u8(HL.low);
                break;
            case 0xA6: // AND (HL)
                and_u8(read(HL.full));
                break;
            case 0xA7: // AND A
                and_u8(A);
                break;
            case 0xA8: // XOR B
                xor_u8(BC.high);
                break;
            case 0xA9: // XOR C
                xor_u8(BC.low);
                break;
            case 0xAA: // XOR D
                xor_u8(DE.high);
                break;
            case 0xAB: // XOR E
                xor_u8(DE.low);
                break;
            case 0xAC: // XOR H
                xor_u8(HL.high);
                break;
            case 0xAD: // XOR L
                xor_u8(HL.low);
                break;
            case 0xAE: // XOR (HL)
                xor_u8(read(HL.full));
                break;
            case 0xAF: // XOR A
                xor_u8(A);
                break;
            case 0xB0: // OR B
                or_u8(BC.high);
                break;
            case 0xB1: // OR C
                or_u8(BC.low);
                break;
            case 0xB2: // OR D
                or_u8(DE.high);
                break;
            case 0xB3: // OR E
                or_u8(DE.low);
                break;
            case 0xB4: // OR H
                or_u8(HL.high);
                break;
            case 0xB5: // OR L
                or_u8(HL.low);
                break;
            case 0xB6: // OR (HL)
                or_u8(read(HL.full));
                break;
            case 0xB7: // OR A
                or_u8(A);
                break;
            case 0xB8: // CP B
                cp_u8(BC.high);
                break;
            case 0xB9: // CP C
                cp_u8(BC.low);
                break;
            case 0xBA: // CP D
                cp_u8(DE.high);
                break;
            case 0xBB: // CP E
                cp_u8(DE.low);
                break;
            case 0xBC: // CP H
                cp_u8(HL.high);
                break;
            case 0xBD: // CP L
                cp_u8(HL.low);
                break;
            case 0xBE: // CP (HL)
                cp_u8(read(HL.full));
                break;
            case 0xBF: // CP A
                cp_u8(A);
                break;
            case 0xC0: // RET NZ
                // Pop 2 bytes from the stack and increase SP (stack grows downwards)
                t_u16.low = read(SP.full++);
                t_u16.high = read(SP.full++);
                if (!F_Z) {
                    PC = t_u16.full;
                    cycles += 12;
                }
                break;
            case 0xC1: // POP BC
                BC.low = read(SP.full++);
                BC.high = read(SP.full++);
                break;
            case 0xC2: // JP NZ,a16
                t_u16.low = read(PC++);
                t_u16.high = read(PC++);
                if (!F_Z) {
                    PC = t_u16.full;
                    cycles += 4;
                }
                break;
            case 0xC3: // JP a16
                t_u16.low = read(PC++);
                t_u16.high = read(PC++);
                PC = t_u16.full;
                break;
            case 0xC4: // CALL NZ,a16
                t_u16.high = read(--SP.full);
                t_u16.low = read(--SP.full);
                if (!F_Z) {
                    PC = t_u16.full;
                    cycles += 12;
                }
                break;
            case 0xC5: // PUSH BC
                write(--SP.full, BC.high);
                write(--SP.full, BC.low);
                break;
            case 0xC6: // ADD A,d8
                add_u8(&A, read(PC++));
                break;
            case 0xC7: // RST 00H
                // push PC onto stack, then jump to address
                write(--SP.full, (PC >> 8) & 0xFF);
                write(--SP.full, (PC & 0xFF));
                PC = 0x0000;
                break;
            case 0xC8: // RET Z
                // Pop 2 bytes from the stack and increase SP (stack grows downwards)
                t_u16.low = read(SP.full++);
                t_u16.high = read(SP.full++);
                if (F_Z) {
                    PC = t_u16.full;
                    cycles += 12;
                }
                break;
            case 0xC9: // RET
                t_u16.low = read(SP.full++);
                t_u16.high = read(SP.full++);
                PC = t_u16.full;
                break;
            case 0xCA: // JP Z,a16
                t_u16.low = read(PC++);
                t_u16.high = read(PC++);
                if (F_Z) {
                    PC = t_u16.full;
                    cycles += 4;
                }
                break;
            case 0xCB: // Prefix CB
                prefix_cb = 1;
                break;
            case 0xCC: // CALL Z,a16
                t_u16.high = read(--SP.full);
                t_u16.low = read(--SP.full);
                if (F_Z) {
                    PC = t_u16.full;
                    cycles += 12;
                }
                break;
            case 0xCD: // CALL a16
                t_u16.high = read(--SP.full);
                t_u16.low = read(--SP.full);
                PC = t_u16.full;
                break;
            case 0xCE: // ADC A,d8
                add_u8(&A, read(PC++) + F_C);
                break;
            case 0xCF: // RST 08H
                // push PC onto stack, then jump to address
                write(--SP.full, (PC >> 8) & 0xFF);
                write(--SP.full, (PC & 0xFF));
                PC = 0x0008;
                break;

            case 0xD0: // RET NC
                // Pop 2 bytes from the stack and increase SP (stack grows downwards)
                t_u16.low = read(SP.full++);
                t_u16.high = read(SP.full++);
                if (!F_C) {
                    PC = t_u16.full;
                    cycles += 12;
                }
                break;
            case 0xD1: // POP DE
                DE.low = read(SP.full++);
                DE.high = read(SP.full++);
                break;
            case 0xD2: // JP NC,a16
                t_u16.low = read(PC++);
                t_u16.high = read(PC++);
                if (!F_C) {
                    PC = t_u16.full;
                    cycles += 4;
                }
                break;
            case 0xD3:
                // nothing here
                break;
            case 0xD4: // CALL NC,a16
                t_u16.high = read(--SP.full);
                t_u16.low = read(--SP.full);
                if (!F_C) {
                    PC = t_u16.full;
                    cycles += 12;
                }
                break;
            case 0xD5: // PUSH DE
                write(--SP.full, DE.high);
                write(--SP.full, DE.low);
                break;
            case 0xD6: // SUB d8
                sub_u8(read(PC++));
                break;
            case 0xD7: // RST 10H
                // push PC onto stack, then jump to address
                write(--SP.full, (PC >> 8) & 0xFF);
                write(--SP.full, (PC & 0xFF));
                PC = 0x0010;
                break;
            case 0xD8: // RET C
                // Pop 2 bytes from the stack and increase SP (stack grows downwards)
                t_u16.low = read(SP.full++);
                t_u16.high = read(SP.full++);
                if (F_C) {
                    PC = t_u16.full;
                    cycles += 12;
                }
                break;
            case 0xD9: // RETI
                t_u16.low = read(SP.full++);
                t_u16.high = read(SP.full++);
                PC = t_u16.full;
                reg[REG_IE] = 1;
                break;
            case 0xDA: // JP C,a16
                t_u16.low = read(PC++);
                t_u16.high = read(PC++);
                if (F_C) {
                    PC = t_u16.full;
                    cycles += 4;
                }
                break;
            case 0xDB:
                // nothing here
                break;
            case 0xDC: // CALL C,a16
                t_u16.high = read(--SP.full);
                t_u16.low = read(--SP.full);
                if (F_C) {
                    PC = t_u16.full;
                    cycles += 12;
                }
                break;
            case 0xDD:
                // nothing here
                break;
            case 0xDE: // SBC A,d8
                sub_u8(read(PC++) - F_C);
                break;
            case 0xDF: // RST 18H
                // push PC onto stack, then jump to address
                write(--SP.full, (PC >> 8) & 0xFF);
                write(--SP.full, (PC & 0xFF));
                PC = 0x0018;
                break;

            case 0xE0: // LDH (a8),A
                // Put A into memory address 0xFF00+n (IO)
                write(MEM_IO + read(PC++), A);
                break;
            case 0xE1: // POP HL
                HL.low = read(SP.full++);
                HL.high = read(SP.full++);
                break;
            case 0xE2: // LD (C),A
                // Put A into memory address 0xFF00+C (IO)
                write(MEM_IO + BC.low, A);
                break;
            case 0xE3:
                // nothing here
                break;
            case 0xE4:
                // nothing here
                break;
            case 0xE5: // PUSH HL
                write(--SP.full, HL.high);
                write(--SP.full, HL.low);
                break;
            case 0xE6: // AND d8
                and_u8(read(PC++));
                break;
            case 0xE7: // RST 20H
                // push PC onto stack, then jump to address
                write(--SP.full, (PC >> 8) & 0xFF);
                write(--SP.full, (PC & 0xFF));
                PC = 0x0020;
                break;
            case 0xE8: // ADD SP,r8
                //add_u8(&SP.full, read(PC++));
                t_s8 = (s8)read(PC++);
                if (t_s8 >= 0) {
                    F_H = HALF_CARRY_U16_ADD(SP.full, t_s8);
                    F_C = CARRY_ADD(SP.full, t_s8);
                }
                else {
                    F_H = HALF_CARRY_U16_SUB(SP.full, t_s8);
                    F_C = CARRY_SUB(SP.full, t_s8);
                }
                F_N = 0;
                F_Z = 0;
                SP.full += t_s8;
                break;
            case 0xE9: // JP (HL)
                PC = HL.full;
                break;
            case 0xEA: // LD (a16),A
                t_u16.low = read(PC++);
                t_u16.high = read(PC++);
                write(t_u16.full, A);
                break;
            case 0xEB:
                // nothing here
                break;
            case 0xEC:
                // nothing here
                break;
            case 0xED:
                // nothing here
                break;
            case 0xEE: // XOR d8
                xor_u8(read(PC++));
                break;
            case 0xEF: // RST 28H
                // push PC onto stack, then jump to address
                write(--SP.full, (PC >> 8) & 0xFF);
                write(--SP.full, (PC & 0xFF));
                PC = 0x0028;
                break;

            case 0xF0: // LDH A,(a8)
                // Put value in memory address 0xFF00+n into A
                t_u8 = read(PC++);
                A = read(MEM_IO + t_u8);
                break;
            case 0xF1: // POP AF
                t_u8 = read(SP.full++);
                F_C = GET_BIT(t_u8, 4);
                F_H = GET_BIT(t_u8, 5);
                F_N = GET_BIT(t_u8, 6);
                F_Z = GET_BIT(t_u8, 7);
                A = read(SP.full++);
                break;
            case 0xF2: // LD A,(C)
                // Put value in memory address 0xFF00+C into A
                A = read(MEM_IO + BC.low);
                break;
            case 0xF3: // DI
                reg[REG_IE] = 0;
                break;
            case 0xF4:
                // nothing here
                break;
            case 0xF5: // PUSH AF
                write(--SP.full, A);
                // reconstruct the F register
                t_u8 = 0;
                t_u8 |= ((F_Z << 7) | (F_N << 6) | (F_H << 5) | (F_C << 4));
                write(--SP.full, t_u8);
                break;
            case 0xF6: // OR d8
                or_u8(read(PC++));
                break;
            case 0xF7: // RST 30H
                // push PC onto stack, then jump to address
                write(--SP.full, (PC >> 8) & 0xFF);
                write(--SP.full, (PC & 0xFF));
                PC = 0x0030;
                break;
            case 0xF8: // LD HL,SP+r8
                t_s8 = (s8)read(PC++);
                t_u16.full = SP.full;
                if (t_s8 >= 0) {
                    F_H = HALF_CARRY_U16_ADD(SP.full, t_s8);
                    F_C = CARRY_ADD(SP.full, t_s8);
                }
                else {
                    F_H = HALF_CARRY_U16_SUB(SP.full, t_s8);
                    F_C = CARRY_SUB(SP.full, t_s8);
                }
                F_N = 0;
                F_Z = 0;
                HL.full = (SP.full + t_s8);
                break;
            case 0xF9: // LD SP,HL
                SP.full = HL.full;
                break;
            case 0xFA: // LD A,(a16)
                t_u16.low = read(PC++);
                t_u16.high = read(PC++);
                A = read(t_u16.full);
                break;
            case 0xFB: // EI
                reg[REG_IE] = 1;
                break;
            case 0xFC:
                // nothing here
                break;
            case 0xFD:
                // nothing here
                break;
            case 0xFE: // CP d8
                cp_u8(read(PC++));
                break;
            case 0xFF: // RST 38H
                // push PC onto stack, then jump to address
                write(--SP.full, (PC >> 8) & 0xFF);
                write(--SP.full, (PC & 0xFF));
                PC = 0x0038;
                break;
        }
    }
    // Prefix CB
    else {
        prefix_cb = 0;
    }
    return cycles;
}


void emu_cpu_update()
{
    u8 op; // the current operand read from memory at PC location
    u8 cycles;
    int cycles_this_update = 0;
    while (cycles_this_update < MAXDOTS)
    {
        // Fetch instruction
        op = read(PC++);
        cycles = execute_instruction(op);
        
        //cycles = 4; // instead of 4, a value is returned by the operand instruction
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
