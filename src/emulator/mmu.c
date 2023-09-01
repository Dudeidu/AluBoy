#include "mmu.h"
#include "macros.h"

#include <stdio.h>
#include <string.h>
#include "alu_io.h"
#include "emu_shared.h"

#include "gb.h"
#include "cpu.h"
#include "input.h"
#include "timer.h"
#include "ppu.h"
#include "apu.h"

u8* rom  = NULL;    // loaded from .gb / .gbc
u8* eram = NULL;    // external ram (cartridge)

u16 rom_banks;      // up to 512 banks of 16 KB each (8MB)
u8  eram_banks;     // up to 16 banks of 8 KB each (128 KB)

u8  mbc;            // 0: ROM only | 1: MBC1 | 2: MBC2 | 3: MBC3 | 4: MMM01 | 5: MBC5 

// MBC Registers
u16 rom_bank;       // current bank
u8  rom_bank_2;     // secondary ROM banking register
u8  eram_bank;
u8  eram_enabled;   // RAM/RTC Enable mbc register
u8  has_battery;    // whether to import/export the external ram to a file (.sav)
u8  mbc_mode;       // For mbc1 only - 0: 2MiB ROM/8KiB RAM | 1: 512KiB ROM/4*8Kib RAM

u8  rtc[0xD];       // Refers to RTCRegister enum
u8  rtc_latch_flag;
u8  rtc_latch_reg;
u8  rtc_select_reg; // Indicated which RTC register is currently mapped into memory at A000 - BFFF

// Header information
unsigned char title[17]; // 16 + '\0'
unsigned char licensee_code_new[2];
u8  licensee_code_old;
u8  destination_code;
u8  cgb_flag;
u8  sgb_flag;
u8  cart_type;
u8  rom_version;
u16 checksum_global;
u8  rom_size_code;
u8  eram_size_code;


int mmu_init(u8* rom_buffer) {
    // Lookup tables for cart type
    u8 mbc_lut[] = {
        0, 1, 1, 1, 0, 2, 2, 0, 0, 0, 0, 4, 4, 4, 0, 3,
        3, 3, 3, 3, 0, 0, 0, 0, 0, 5, 5, 5, 5, 5, 5
    };
    u8 mbc_lut_size = 31;

    rom = rom_buffer;

    // Get ROM header data
    mbc_mode = 0;
    rom_bank = 1;
    rom_bank_2 = 0;
    eram_enabled = 0;

    // Title
    memcpy(title, &rom[ROM_TITLE], sizeof(unsigned char) * 16);
    title[16] = '\0'; // adds a string terminator
    printf("Title: %s\n", title);

    // CGB Indicator
    cgb_flag = rom[ROM_CGB_FLAG] == 0x80;
    cgb_mode = 0;
    printf("CGB: %s\n", cgb_flag ? "true" : "false");

    // SGB Indicator
    sgb_flag = rom[ROM_SGB_FLAG] == 0x03;
    printf("SGB: %s\n", sgb_flag ? "true" : "false");

    // Cart type
    cart_type = rom[ROM_CART_TYPE];
    
    switch (cart_type) {
        case 0x03: case 0x06: case 0x09: case 0x0D: case 0x0F: 
        case 0x10: case 0x13: case 0x1B: case 0x1E: case 0x22: case 0xFF:
            has_battery = 1;
            break;
        default:
            has_battery = 0;
            break;
    }

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
        case 0x8:   rom_banks = 512; break; // 8   MB
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

    if (eram != NULL) {
        free(eram);
        eram = NULL;
    }
    if (eram_banks > 0) {
        size_t buffer_size = sizeof(u8) * eram_banks * BANKSIZE_ERAM;
        eram = (u8*)malloc(buffer_size);
        if (eram == NULL)
        {
            fprintf(stderr, "Failed to allocate memory for ERAM buffer!\n");
            return 0;
        }
    }
    else {
        // MBC2 has built in ram (512x4 bits)
        if (mbc == 2) {
            eram = (u8*)malloc(512);
            if (eram == NULL)
            {
                fprintf(stderr, "Failed to allocate memory for ERAM buffer!\n");
                return 0;
            }
        }
    }

    // Licensee code
    memcpy(licensee_code_new, &rom[ROM_LICENSEE_NEW], sizeof(unsigned char) * 2); // Stored as 2 char ascii
    printf("Licensee new: %c%c\n", licensee_code_new[0], licensee_code_new[1]);
    licensee_code_old = rom[ROM_LICENSEE_OLD];
    printf("Licensee old: %02X\n", licensee_code_old);
    
    // Misc
    destination_code    = rom[ROM_DESTINATION];
    rom_version         = rom[ROM_VERSION];
    checksum_header     = rom[ROM_HEADER_CHECKSUM];
    checksum_global     = rom[ROM_GLOBAL_CHECKSUM] << 8 || rom[ROM_GLOBAL_CHECKSUM + 1];

    printf("Cart type: %d\nMBC: %d\nROM banks: %d\nERAM banks: %d\n\n", cart_type, mbc, rom_banks, eram_banks);

    return 1;
}

void mmu_powerup()
{
    // load save file if exists
    if (has_battery) {
        size_t buffer_size = (mbc == 2) ? 512 : sizeof(u8) * eram_banks * BANKSIZE_ERAM;

        // Load save
        const char* path_arr[] = { rom_file_path, rom_file_name, ".sav"};
        char* save_path = combine_strings(path_arr, 3);
        char* save_buffer = NULL;
        if (save_path != NULL) {
            save_buffer = LoadBuffer(save_path);
            free(save_path);
        }
        else {
            printf("no save data found.\n");
        }
        if (save_buffer != NULL && eram != NULL)
        {
            memcpy(eram, save_buffer, buffer_size);
            free(save_buffer);

            printf("save loaded from disk.\n");
        }
    }
}


// Dump ERAM to [.sav] file
void save() {
    if (!has_battery) return;

    const char* path_arr[] = { rom_file_path, rom_file_name, ".sav"};
    char* save_path = combine_strings(path_arr, 3);
    if (save_path != NULL) {
        size_t buffer_size = sizeof(u8) * eram_banks * BANKSIZE_ERAM;
        int success;
        success = SaveBuffer(save_path, eram, buffer_size);
        if (success) {
            printf("save written to disk.\n");
        }
        else {
            printf("could not write save to disk.\n");
        }
        free(save_path);
    }
}

u8 read(u16 addr)
{
    u8 msb = (u8)(addr >> 12);

    if (oam_dma_transfer_flag && (addr != (0xFF00 | REG_DMA)) && !oam_dma_access_flag && (addr < MEM_HRAM || addr >= MEM_IE)) 
        return 0xFF;

    switch (msb) {
        case 0x0:
        case 0x1:
        case 0x2:
        case 0x3:
            // In MBC1 mode-1: ROM bank X0
            if (mbc == 1 && mbc_mode == 1) {
                return rom[addr + ((rom_bank_2 << 5) % rom_banks) * BANKSIZE_ROM];
            }
            // ROM bank 0
            else {
                return rom[addr];
            }
        case 0x4:
        case 0x5:
        case 0x6:
        case 0x7:
            return rom[(addr - MEM_ROM_N) + (rom_bank * BANKSIZE_ROM)];
        case 0xA:
        case 0xB:
            // RAM bank 00-03, if any
            if (!eram_enabled) return 0xFF;
            if (mbc == 2) {
                // Half bytes, Bottom 9 bits of address are used to index RAM
                return (eram[addr & 0x1FF] | 0xF0); // reads first nibble, second nibble is all set
            }
            else if (mbc == 3 && rtc_select_reg > 0) {
                // RTC register read
                return rtc[rtc_select_reg];
            }
            else {
                if (eram_bank >= eram_banks) return 0xFF;
                return eram[(addr & 0x1FFF) + (eram_bank * BANKSIZE_ERAM)];
            }
        default:
            if (addr >= MEM_IO && addr < MEM_HRAM) {
                u8 reg_id = addr & 0xFF;

                // Timer registers
                if (reg_id >= REG_DIV && reg_id <= REG_TAC)
                    return timer_read_register(reg_id);
                // APU registers
                else if (reg_id >= REG_NR10 && reg_id < REG_LCDC)
                    return apu_read_register(reg_id);
                // PPU registers
                else if ((reg_id >= REG_LCDC && reg_id <= REG_WX) || (reg_id >= REG_BGPI && reg_id <= REG_OPRI))
                    return ppu_read_register(reg_id);
                // CPU registers
                else
                    return cpu_read_register(reg_id);
            }
            else {
                return cpu_read_memory(addr);
            }
    }
    return 0xFF;
}

void write(u16 addr, u8 value)
{
    // TODO - I/O register writing rules
    u8 msb = (u8)(addr >> 12);

    if ((addr != (0xFF00 | REG_DMA)) && oam_dma_transfer_flag && !oam_dma_access_flag && (addr < MEM_HRAM || addr >= MEM_IE)) {
        tick();
        return;
    }

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
                        if (rb == 0) rb = 1;
                        //printf("bank change value: %02X,", rb);
                        if (rb < rom_banks) {
                            rom_bank = (rom_bank & ~0x1F) | rb; // only change the lower 5 bits
                        }
                        else {
                            // Mask the number to the max banks
                            rom_bank = (rom_bank & ~0x1F) | (rb & (rom_banks-1));

                        }
                        //printf("new rom bank: %02X\n", rom_bank);
                    } break;
                    case 0x4:
                    case 0x5:
                    {
                        // 2 bit register - RAM bank number / Upper bits of ROM bank number
                        rom_bank_2 = (value & 0x3);
                        rom_bank = (rom_bank & 0x1F) | (rom_bank_2 << 5);

                        // Mask the number to the max banks
                            rom_bank = (rom_bank & (rom_banks-1));

                        if (mbc_mode == 1) {
                            eram_bank = (value & 0x3);
                        }
                        
                        // In MMM01 this 2-bit register is instead applied to bits 4-5 
                        // of the ROM bank number and the top bit of the main 5-bit main ROM banking register is ignored
                    } break;
                    case 0x6:
                    case 0x7:
                    {
                        // 1 bit register - Banking mode select
                        // 00 = Simple Banking Mode (default) 
                        // 01 = RAM Banking Mode / Advanced ROM Banking Mode
                        mbc_mode = (value & 1);
                        if (mbc_mode == 0) {
                            //eram_bank = 0;
                            rom_bank = rom_bank & 0x1F; // remove rom_bank_2
                        }
                        else {
                            u8 i = 0;
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
                        if ((addr & 0x100) == 0) { // bit 8 is 0
                            eram_enabled = ((value & 0xF) == 0xA);
                        }
                        else {
                            //printf("rom bank value: %d,", value);
                            rom_bank = (value & 0xF);
                            if (rom_bank == 0) rom_bank = 1;

                            rom_bank = rom_bank & (rom_banks - 1);
                            //printf("new value: %d\n", rom_bank);
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
                        // Mask the number to the max banks
                        rom_bank = (rom_bank & 0xFF00) | value;
                        rom_bank = rom_bank & (rom_banks - 1);
                    } break;
                    case 0x3:
                    {
                        // bit 8 of ROM bank number
                        rom_bank_2 = (value & 1);
                        rom_bank = (rom_bank & 0xFF) | (rom_bank_2 << 8);
                        // Mask the number to the max banks
                        rom_bank = (rom_bank & (rom_banks-1));
                    } break;
                    case 0x4:
                    case 0x5:
                    {
                        // RAM bank number
                        if (eram_banks >= (value & 0xF)) eram_bank = (value & 0xF);
                    } break;
                }
            } break;
        }
    }
    else {
        switch (msb) {
            case 0xA:
            case 0xB:
                // ERAM
                if (eram_enabled) {
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
                        if (eram_bank < eram_banks)
                            eram[(addr & 0x1FFF) + (eram_bank * BANKSIZE_ERAM)] = value;
                    }
                }
                break;
            default:
                if (addr >= MEM_IO && addr < MEM_HRAM) {
                    u8 reg_id = addr & 0xFF;

                    // Timer registers
                    if (reg_id >= REG_DIV && reg_id <= REG_TAC)
                        timer_write_register(reg_id, value);
                    // APU registers ( TODO 0X76 0x77 for cgb-only PCM registers)
                    else if (reg_id >= REG_NR10 && reg_id < REG_LCDC)
                        apu_write_register(reg_id, value);
                    // PPU registers
                    else if ((reg_id >= REG_LCDC && reg_id <= REG_WX) || (reg_id >= REG_BGPI && reg_id <= REG_OPRI))
                        ppu_write_register(reg_id, value);
                    // CPU registers
                    else
                        cpu_write_register(reg_id, value);
                }
                else {
                    cpu_write_memory(addr, value);
                }
                break;
        }
    }
    
    tick(); // advance the clock 1 M-cycle
}

void mmu_cleanup() {
    if (rom) free(rom);
    if (eram) {
        save();
        free(eram);
    }
}