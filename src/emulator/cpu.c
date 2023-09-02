/// Emulates the Game Boy's DMG/CGB-CPU 

#include "cpu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alu_binary.h"
#include "macros.h"

#include "emu_shared.h"
#include "gb.h"
#include "mmu.h"
#include "ppu.h"    // lcd_mode

typedef struct Flags {
    u8 Z; // Zero flag
    u8 N; // Subtract flag
    u8 H; // Half carry flag
    u8 C; // Carry flag 
} Flags;

// Determines how many CPU cycles each instruction takes to perform
u8 op_cycles_lut[]  = {
     4,12, 8, 8, 4, 4, 8, 4,20, 8, 8, 8, 4, 4, 8, 4,
     4,12, 8, 8, 4, 4, 8, 4,12, 8, 8, 8, 4, 4, 8, 4,
     8,12, 8, 8, 4, 4, 8, 4, 8, 8, 8, 8, 4, 4, 8, 4,
     8,12, 8, 8,12,12,12, 4, 8, 8, 8, 8, 4, 4, 8, 4,
     4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
     4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
     4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
     8, 8, 8, 8, 8, 8, 4, 8, 4, 4, 4, 4, 4, 4, 8, 4,
     4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
     4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
     4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
     4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
     8,12,12,16,12,16, 8,16, 8,16,12, 4,12,24, 8,16,
     8,12,12, 0,12,16, 8,16, 8,16,12, 0,12, 0, 8,16,
    12,12, 8, 0, 0,16, 8,16,16, 4,16, 0, 0, 0, 8,16,
    12,12, 8, 4, 0,16, 8,16,12, 8,16, 4, 0, 0, 8,16
};

// Define shared memory
u8  reg[0x100];     // Refers to Register enum
u8  vram[2 * BANKSIZE_VRAM];
u8  oam[0xA0];

// Hardware registers
u8  A;   // Accumulator
Flags F;           
BytePair BC;
BytePair DE;
BytePair HL;
BytePair SP;        // Stack Pointer
u16 PC;             // Program Counter/Pointer

// CPU specific memory
u8  wram[8 * BANKSIZE_ROM];
u8  hram[0x80];

u8  halted;
u8  ei_flag;
u8  interrupts_enabled; // IME flag

// Tests

int counter = 1;
u8  debug_show_tracelog = 0;
int debug_tracelog_interval = 1;
int debug_tracelog_start = 0;


// Forward declarations

u8 do_interrupts();
u8 execute_instruction(u8 op);
u8 execute_cb(u8 op);

// Arithmetic

void inc_u8(u8* a, Flags* flags) {
    flags->H = HALF_CARRY_U8_ADD(*a, 1);
    
    (*a)++;

    // update other flags
    flags->Z = (*a == 0);
    flags->N = 0;
}
void dec_u8(u8* a, Flags* flags) {
    flags->H = HALF_CARRY_U8_SUB(*a, 1);
    
    (*a)--;

    // update other flags
    flags->N = 1;
    flags->Z = (*a == 0);
}
void add_u8(u8* a, u8 b, Flags* flags) {
    flags->H = HALF_CARRY_U8_ADD(*a, b);
    flags->C = CARRY_ADD(*a, b);
    
    (*a) += b;

    // update other flags
    flags->N = 0;
    flags->Z = (*a == 0);
}
void adc_u8(u8* a, u8 b, Flags* flags) {
    u8 carry = flags->C;        // Store the carry flag
    int sum = *a + b + carry;   // Calculate the sum
    u8 result = (u8)sum;

    flags->H = (((*a & 0xF) + (b & 0xF) + carry) > 0xF);
    flags->C = (sum > 0xFF);    // Update the carry flag
    
    (*a) = result;              // Store the result in *a

    // update other flags
    flags->N = 0;               // Set the subtraction flag
    flags->Z = (A == 0);
}
void add_u16(u16* aa, u16 bb, Flags* flags) {
    flags->H = HALF_CARRY_U16_ADD(*aa, bb);
    flags->C = CARRY_ADD_U16(*aa, bb);
    
    (*aa) += bb;

    // update other flags
    flags->N = 0;
}
void sub_u8(u8* a, u8 b, Flags* flags) {
    flags->H = HALF_CARRY_U8_SUB(*a, b);
    flags->C = CARRY_SUB(*a, b);
    
    (*a) -= b;

    // update other flags
    flags->Z = (*a == 0);
    flags->N = 1;
}
void sbc_u8(u8* a, u8 b, Flags* flags) {
    u8 carry = flags->C;  // Store the carry flag
    int diff = *a - b - carry;  // Calculate the difference with borrow
    u8 result = (u8)diff;

    flags->H = (((*a & 0xF) - (b & 0xF) - carry) < 0);
    flags->C = (diff < 0);  // Update the carry flag
    
    (*a) = result;  // Store the result in *a
    
    // update other flags
    flags->N = 1;  // Set the subtraction flag
    flags->Z = (*a == 0);
}
void cp_u8(u8* a, u8 b, Flags* flags) {
    // Same as sub, but discards the results and only updates the flags
    flags->H = HALF_CARRY_U8_SUB(*a, b);
    flags->C = CARRY_SUB(*a, b);
    flags->N = 1;
    flags->Z = (*a - b == 0);
}
void and_u8(u8* a, u8 b, Flags* flags) {
    (*a) &= b;

    // update flags
    flags->Z = (*a == 0);
    flags->H = 1;
    flags->C = 0;
    flags->N = 0;
}
void xor_u8(u8* a, u8 b, Flags* flags) {
    (*a) ^= b;

    // update flags
    flags->Z = (*a == 0);
    flags->H = 0;
    flags->C = 0;
    flags->N = 0;
}
void or_u8(u8* a, u8 b, Flags* flags) {
    (*a) |= b;

    // update flags
    flags->Z = (*a == 0);
    flags->H = 0;
    flags->C = 0;
    flags->N = 0;
}

// Rotates & Shifts 

void rlc(u8* a, Flags* flags) {
    // rotate left carry
    flags->C = GET_BIT(*a, 7);
    *a = ROTATE_LEFT(*a, 1, 8);

    // update other flags
    flags->Z = (*a == 0);
    flags->N = 0;
    flags->H = 0;
}
void rrc(u8* a, Flags* flags) {
    // rotate right carry
    flags->C = GET_BIT(*a, 0);
    *a = ROTATE_RIGHT(*a, 1, 8);

    // update other flags
    flags->Z = (*a == 0);
    flags->N = 0;
    flags->H = 0;
}
void rl(u8* a, Flags* flags) {
    // rotate left
    u8 temp = flags->C;
    flags->C = GET_BIT(*a, 7);
    *a = (*a << 1) & 0xFF;
    if (temp) SET_BIT(*a, 0);

    // update other flags
    flags->Z = (*a == 0);
    flags->N = 0;
    flags->H = 0;
}
void rr(u8* a, Flags* flags) {
    // rotate right
    u8 temp = flags->C;
    flags->C = GET_BIT(*a, 0);  // store lsb in carry flag
    *a = (*a >> 1) & 0xFF;      // right shift
    if (temp) SET_BIT(*a, 7);   // set msb from old carry

    // update other flags
    flags->Z = (*a == 0);
    flags->N = 0;
    flags->H = 0;
}
void sla(u8* a, Flags* flags) {
    // shift left arithmetic
    // left shift into carry, conserving the msb
    flags->C = GET_BIT(*a, 7);  // store msb in carry flag
    *a = (*a << 1) & 0xFF;      // left shift

    // update other flags
    flags->Z = (*a == 0);
    flags->N = 0;
    flags->H = 0;
}
void sra(u8* a, Flags* flags) {
    // shift right arithmetic
    // right shift into carry, conserving the msb
    u8 temp = GET_BIT(*a, 7);   // conserve most significant bit
    flags->C = GET_BIT(*a, 0);  // store lsb in carry flag
    *a = (*a >> 1) & 0xFF;      // shift right
    // restore msb
    if (temp)   SET_BIT(*a, 7);
    else        RESET_BIT(*a, 7);

    // update other flags
    flags->Z = (*a == 0);
    flags->N = 0;
    flags->H = 0;
}
void srl(u8* a, Flags* flags) {
    // shift right logical
    // right shift into carry, msb set to 0
    flags->C = GET_BIT(*a, 0);  // load lsb to carry
    *a = (*a >> 1) & 0xFF;      // shift right
    // msb set to 0
    RESET_BIT(*a, 7);

    // set other flags
    flags->Z = (*a == 0);
    flags->N = 0;
    flags->H = 0;
}
void swap(u8* a, Flags* flags) {
    // swap low and high nibbles (4 bits)
    *a = ((*a & 0xF) << 4) | (*a >> 4);
    
    // update flags
    flags->Z = (*a == 0);
    flags->N = 0;
    flags->H = 0;
    flags->C = 0;
}
void test_bit(u8* a, u8 b, Flags* flags) {
    // update the flags depending on the bit value
    flags->Z = !GET_BIT(*a, b);
    flags->N = 0;
    flags->H = 1;
}


void cpu_powerup()
{
    // Reset registers to their default values (DMG)
    A = 0x01;

    F.Z = 1;
    F.N = 0;
    if (checksum_header == 0)
    {
        F.H = 0;
        F.C = 0;
    }
    else {
        F.H = 1;
        F.C = 1;
    }

    BC.full = 0x0013;
    DE.full = 0x00D8;
    HL.full = 0x014D;
    SP.full = 0xFFFE;
    PC = 0x0100;

    interrupts_enabled = 0;
    ei_flag = 0;
    halted = 0;

    for (int i = 0; i <= 0xFF; i++) {
        reg[i] = 0xFF;
    }
    reg[REG_P1] = 0xCF;
    reg[REG_SB] = 0x00;
    reg[REG_SC] = 0x7E;

    reg[REG_IF] = 0xE1;

    reg[REG_KEY1] = 0xFF;

    reg[REG_VBK] = 0x0;
    reg[REG_HDMA1] = 0xFF;
    reg[REG_HDMA2] = 0xFF;
    reg[REG_HDMA3] = 0xFF;
    reg[REG_HDMA4] = 0xFF;
    reg[REG_HDMA5] = 0xFF;

    reg[REG_RP] = 0xFF;

    reg[REG_SVBK] = 0x00;

    reg[REG_IE] = 0x00;
}
int cpu_init()
{

    return 1;
}


u8 cpu_read_register(u8 reg_id) {
    // handles special cases
    switch (reg_id) {
        case REG_P1:
            return reg[REG_P1];
        case REG_SB:
            return 0xFF;

        default:
            return reg[reg_id];
    }
}
void cpu_write_register(u8 reg_id, u8 value) {
    // handles special cases
    switch (reg_id) {
        case REG_P1:
            // only change bits 4 and 5 (rest are read-only)
            reg[REG_P1] = (reg[REG_P1] & ~0x30) | ((value >> 4) & 0x3) << 4;
            break;
        case REG_IF:
            reg[REG_IF] = (reg[REG_IF] & 0xF0) | (value & 0x0F);
            break;
        case 0x50: // BOOT register (read only)
            break;

        default:
            reg[reg_id] = value;   // Convert to range 0-255
            break;
    }
}
u8 cpu_read_memory(u16 addr) {
    u8 msb = (u8)(addr >> 12);

    switch (msb) {
        case 0x8:
        case 0x9:
            // VRAM
            //if (lcd_mode == LCD_MODE_VRAM) return 0xFF;

            if (cgb_mode)   return vram[(addr & 0x1FFF) + (GET_BIT(reg[REG_VBK], 0) * BANKSIZE_VRAM)];
            else            return vram[addr & 0x1FFF];
        case 0xC:
        case 0xE:
            // WRAM / ECHO RAM
            return wram[addr & 0xFFF];
        case 0xD:
        case 0xF:
            // WRAM Bank / ECHO RAM
            if (addr < MEM_OAM) {
                if (cgb_mode)   return wram[(addr & 0x1FFF) + (reg[REG_SVBK] * BANKSIZE_WRAM)];
                else            return wram[addr & 0x1FFF];
            }
            // Object attribute memory (OAM)
            else if (addr >= MEM_OAM && addr < MEM_UNUSABLE) {
                //if (lcd_mode == LCD_MODE_VRAM || lcd_mode == LCD_MODE_OAM) return 0xFF;

                return oam[addr - MEM_OAM]; // Convert to range 0-159
            }
            // Unusable area
            else if (addr >= MEM_UNUSABLE && addr < MEM_IO) {
                //if (lcd_mode == LCD_MODE_VRAM || lcd_mode == LCD_MODE_OAM)
                //{
                //    // TODO OAM corruption
                //    return 0xFF;
                //}
                return 0x00;
            }
            // High RAM
            else if (addr >= MEM_HRAM && addr < MEM_IE) {
                return hram[addr - MEM_HRAM];  // Convert to range 0-127
            }
            // Interrupt Enable register (IE)
            else if (addr == MEM_IE) {
                return reg[REG_IE];
            }
    }
    return 0xFF;
}
void cpu_write_memory(u16 addr, u8 value) {
    // TODO - I/O register writing rules
    u8 msb = (u8)(addr >> 12);

    switch (msb) {
        case 0x8:
        case 0x9:
            // VRAM
            //if (lcd_mode != LCD_MODE_VRAM) {
                if (cgb_mode)   vram[(addr & 0x1FFF) + (GET_BIT(reg[REG_VBK], 0) * BANKSIZE_VRAM)] = value;
                else            vram[addr & 0x1FFF] = value;
            //}
            //else {
            //    printf("attempting to write to VRAM on LCD_MODE_VRAM! LY=%d\n", reg[REG_LY]);
            //}
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
                if (cgb_mode)   wram[(addr & 0x1FFF) + (reg[REG_SVBK] * BANKSIZE_WRAM)] = value;
                else            wram[addr & 0x1FFF] = value;
            }
            // Object attribute memory (OAM)
            else if (addr >= MEM_OAM && addr < MEM_UNUSABLE) {
                //if (lcd_mode != LCD_MODE_VRAM && lcd_mode != LCD_MODE_OAM) {
                    oam[addr - MEM_OAM] = value; // Convert to range 0-159
                //}
            }
            // High RAM
            else if (addr >= MEM_HRAM && addr < MEM_IE) {
                hram[addr - MEM_HRAM] = value;  // Convert to range 0-127
            }
            // Interrupt Enable register (IE)
            else if (addr == MEM_IE){
                reg[REG_IE] = value;
            }
            break;
    }

    tick(); // advance the clock 1 M-cycle
}

u8 cpu_update() {
    u8 op; // the current operand read from memory at PC location
    u8 cycles;

    if (debug_show_tracelog) {
        if (counter >= debug_tracelog_start && counter % debug_tracelog_interval == 0)
        {
            printf("%06d [%04x] (%02X %02X %02X %02X)  AF=%04x BC=%04x DE=%04x HL=%04x SP=%04x P1=%04X LCD:%d IME:%d IE=%02X IF=%02X HALT=%d DIV=%02X TIMA=%02X\n",
                counter, PC, read(PC), read(PC + 1), read(PC + 2), read(PC + 3), (A << 8) | ((F.Z << 7) | (F.N << 6) | (F.H << 5) | (F.C << 4)),
                BC.full, DE.full, HL.full, SP.full, reg[REG_P1], lcd_enabled, interrupts_enabled, reg[REG_IE], reg[REG_IF], halted, reg[REG_DIV], reg[REG_TIMA]);
        }
        counter++;
    }

    if (halted) op = 0x00; // NOOP
    else op = read(PC++);
    tick();

    cycles = execute_instruction(op);

    // Interrupt handling
    cycles += do_interrupts();
    // The effect of the EI instruction is delayed by one instruction
    if (ei_flag) {
        ei_flag = 0;
        interrupts_enabled = 1;
    }

    return (cycles);
}


inline u8 interrupt_is_pending() {
    return (((reg[REG_IE] & 0x1F) & (reg[REG_IF] & 0x1F)) != 0);
}
u8 do_interrupts() {
    u8 cycles = 0;
    if (!interrupt_is_pending()) return cycles;

    if (halted) {
        halted = 0; // The CPU wakes up
        //printf("CPU woke up\n");
    }
    //printf("%d", reg[REG_IE]);
    // The interrupt handler is called normally
    if (interrupts_enabled) {
        // Interrupt priority
        for (u8 i = 0; i <= 4; i++) {
            if (GET_BIT(reg[REG_IF], i) && GET_BIT(reg[REG_IE], i)) {
                RESET_BIT(reg[REG_IF], i);
                interrupts_enabled = 0;
                if (i == 4) {
                    u8 i = 0;
                }
                // CALL interrupt vector
                // push PC onto stack, then jump to address
                tick();
                tick();
                write(--SP.full, (PC >> 8) & 0xFF);
                write(--SP.full, (PC & 0xFF));
                switch (i) {
                    case 0: 
                        PC = INT_VEC_VBLANK; break; //printf("vb\n"); break;
                    case 1: 
                        PC = INT_VEC_STAT; 
                        //check_stat_irq(0);
                        break; //printf("st\n");break;
                    case 2: PC = INT_VEC_TIMER;  break; //printf("tm\n");break;
                    case 3: PC = INT_VEC_SERIAL; break; //printf("sr\n");break;
                    case 4: PC = INT_VEC_JOYPAD; break; //printf("jp\n");break;
                }
                tick();
                cycles = 20;
                break;
            }
        }
    }
    // TODO - halt bug
    else {
        

    }
    return cycles;
}


u8 execute_cb(u8 op) {
    // Prefix CB
    u8 cycles;
    u8 t_u8;

    if ((op & 0xF) == 0x6 || (op & 0xF) == 0xE) // (HL) operations
    {
        u8 un = op >> 4; // upper nibblet
        cycles = (un >= 4 && un <= 7) ? 12 : 16; // BIT n,(HL) = 12 cycles, rest are 16
    }
    else cycles = 8;

    switch (op)
    {
        case 0x00: // RLC B
            rlc(&BC.high, &F);
            break;
        case 0x01: // RLC C
            rlc(&BC.low, &F);
            break;
        case 0x02: // RLC D
            rlc(&DE.high, &F);
            break;
        case 0x03: // RLC E
            rlc(&DE.low, &F);
            break;
        case 0x04: // RLC H
            rlc(&HL.high, &F);
            break;
        case 0x05: // RLC L
            rlc(&HL.low, &F);
            break;
        case 0x06: // RLC (HL)
            t_u8 = read(HL.full); tick();
            rlc(&t_u8, &F);
            write(HL.full, t_u8);
            break;
        case 0x07: // RLC A
            rlc(&A, &F);
            break;
        case 0x08: // RRC B
            rrc(&BC.high, &F);
            break;
        case 0x09: // RRC C
            rrc(&BC.low, &F);
            break;
        case 0x0A: // RRC D
            rrc(&DE.high, &F);
            break;
        case 0x0B: // RRC E
            rrc(&DE.low, &F);
            break;
        case 0x0C: // RRC H
            rrc(&HL.high, &F);
            break;
        case 0x0D: // RRC L
            rrc(&HL.low, &F);
            break;
        case 0x0E: // RRC (HL)
            t_u8 = read(HL.full); tick();
            rrc(&t_u8, &F);
            write(HL.full, t_u8);
            break;
        case 0x0F: // RRC A
            rrc(&A, &F);
            break;
        case 0x10: // RL B
            rl(&BC.high, &F);
            break;
        case 0x11: // RL C
            rl(&BC.low, &F);
            break;
        case 0x12: // RL D
            rl(&DE.high, &F);
            break;
        case 0x13: // RL E
            rl(&DE.low, &F);
            break;
        case 0x14: // RL H
            rl(&HL.high, &F);
            break;
        case 0x15: // RL L
            rl(&HL.low, &F);
            break;
        case 0x16: // RL (HL)
            t_u8 = read(HL.full); tick();
            rl(&t_u8, &F);
            write(HL.full, t_u8);
            break;
        case 0x17: // RL A
            rl(&A, &F);
            break;
        case 0x18: // RR B
            rr(&BC.high, &F);
            break;
        case 0x19: // RR C
            rr(&BC.low, &F);
            break;
        case 0x1A: // RR D
            rr(&DE.high, &F);
            break;
        case 0x1B: // RR E
            rr(&DE.low, &F);
            break;
        case 0x1C: // RR H
            rr(&HL.high, &F);
            break;
        case 0x1D: // RR L
            rr(&HL.low, &F);
            break;
        case 0x1E: // RR (HL)
            t_u8 = read(HL.full); tick();
            rr(&t_u8, &F);
            write(HL.full, t_u8);
            break;
        case 0x1F: // RR A
            rr(&A, &F);
            break;
        case 0x20: // SLA B
            sla(&BC.high, &F);
            break;
        case 0x21: // SLA C
            sla(&BC.low, &F);
            break;
        case 0x22: // SLA D
            sla(&DE.high, &F);
            break;
        case 0x23: // SLA E
            sla(&DE.low, &F);
            break;
        case 0x24: // SLA H
            sla(&HL.high, &F);
            break;
        case 0x25: // SLA L
            sla(&HL.low, &F);
            break;
        case 0x26: // SLA (HL)
            t_u8 = read(HL.full); tick();
            sla(&t_u8, &F);
            write(HL.full, t_u8);
            break;
        case 0x27: // SLA A
            sla(&A, &F);
            break;
        case 0x28: // SRA B
            sra(&BC.high, &F);
            break;
        case 0x29: // SRA C
            sra(&BC.low, &F);
            break;
        case 0x2A: // SRA D
            sra(&DE.high, &F);
            break;
        case 0x2B: // SRA E
            sra(&DE.low, &F);
            break;
        case 0x2C: // SRA H
            sra(&HL.high, &F);
            break;
        case 0x2D: // SRA L
            sra(&HL.low, &F);
            break;
        case 0x2E: // SRA (HL)
            t_u8 = read(HL.full); tick();
            sra(&t_u8, &F);
            write(HL.full, t_u8);
            break;
        case 0x2F: // SRA A
            sra(&A, &F);
            break;
        case 0x30: // SWAP B
            swap(&BC.high, &F);
            break;
        case 0x31: // SWAP C
            swap(&BC.low, &F);
            break;
        case 0x32: // SWAP D
            swap(&DE.high, &F);
            break;
        case 0x33: // SWAP E
            swap(&DE.low, &F);
            break;
        case 0x34: // SWAP H
            swap(&HL.high, &F);
            break;
        case 0x35: // SWAP L
            swap(&HL.low, &F);
            break;
        case 0x36: // SWAP (HL)
            t_u8 = read(HL.full); tick();
            swap(&t_u8, &F);
            write(HL.full, t_u8);
            break;
        case 0x37: // SWAP A
            swap(&A, &F);
            break;
        case 0x38: // SRL B
            srl(&BC.high, &F);
            break;
        case 0x39: // SRL C
            srl(&BC.low, &F);
            break;
        case 0x3A: // SRL D
            srl(&DE.high, &F);
            break;
        case 0x3B: // SRL E
            srl(&DE.low, &F);
            break;
        case 0x3C: // SRL H
            srl(&HL.high, &F);
            break;
        case 0x3D: // SRL L
            srl(&HL.low, &F);
            break;
        case 0x3E: // SRL (HL)
            t_u8 = read(HL.full); tick();
            srl(&t_u8, &F);
            write(HL.full, t_u8);
            break;
        case 0x3F: // SRL A
            srl(&A, &F);
            break;
        case 0x40: // BIT 0,B
            test_bit(&BC.high, 0, &F);
            break;
        case 0x41: // BIT 0,C
            test_bit(&BC.low, 0, &F);
            break;
        case 0x42: // BIT 0,D
            test_bit(&DE.high, 0, &F);
            break;
        case 0x43: // BIT 0,E
            test_bit(&DE.low, 0, &F);
            break;
        case 0x44: // BIT 0,H
            test_bit(&HL.high, 0, &F);
            break;
        case 0x45: // BIT 0,L
            test_bit(&HL.low, 0, &F);
            break;
        case 0x46: // BIT 0,(HL)
            t_u8 = read(HL.full); tick();
            test_bit(&t_u8, 0, &F);
            break;
        case 0x47: // BIT 0,A
            test_bit(&A, 0, &F);
            break;
        case 0x48: // BIT 1,B
            test_bit(&BC.high, 1, &F);
            break;
        case 0x49: // BIT 1,C
            test_bit(&BC.low, 1, &F);
            break;
        case 0x4A: // BIT 1,D
            test_bit(&DE.high, 1, &F);
            break;
        case 0x4B: // BIT 1,E
            test_bit(&DE.low, 1, &F);
            break;
        case 0x4C: // BIT 1,H
            test_bit(&HL.high, 1, &F);
            break;
        case 0x4D: // BIT 1,L
            test_bit(&HL.low, 1, &F);
            break;
        case 0x4E: // BIT 1,(HL)
            t_u8 = read(HL.full); tick();
            test_bit(&t_u8, 1, &F);
            break;
        case 0x4F: // BIT 1,A
            test_bit(&A, 1, &F);
            break;
        case 0x50: // BIT 2,B
            test_bit(&BC.high, 2, &F);
            break;
        case 0x51: // BIT 2,C
            test_bit(&BC.low, 2, &F);
            break;
        case 0x52: // BIT 2,D
            test_bit(&DE.high, 2, &F);
            break;
        case 0x53: // BIT 2,E
            test_bit(&DE.low, 2, &F);
            break;
        case 0x54: // BIT 2,H
            test_bit(&HL.high, 2, &F);
            break;
        case 0x55: // BIT 2,L
            test_bit(&HL.low, 2, &F);
            break;
        case 0x56: // BIT 2,(HL)
            t_u8 = read(HL.full); tick();
            test_bit(&t_u8, 2, &F);
            break;
        case 0x57: // BIT 2,A
            test_bit(&A, 2, &F);
            break;
        case 0x58: // BIT 3,B
            test_bit(&BC.high, 3, &F);
            break;
        case 0x59: // BIT 3,C
            test_bit(&BC.low, 3, &F);
            break;
        case 0x5A: // BIT 3,D
            test_bit(&DE.high, 3, &F);
            break;
        case 0x5B: // BIT 3,E
            test_bit(&DE.low, 3, &F);
            break;
        case 0x5C: // BIT 3,H
            test_bit(&HL.high, 3, &F);
            break;
        case 0x5D: // BIT 3,L
            test_bit(&HL.low, 3, &F);
            break;
        case 0x5E: // BIT 3,(HL)
            t_u8 = read(HL.full); tick();
            test_bit(&t_u8, 3, &F);
            break;
        case 0x5F: // BIT 3,A
            test_bit(&A, 3, &F);
            break;
        case 0x60: // BIT 4,B
            test_bit(&BC.high, 4, &F);
            break;
        case 0x61: // BIT 4,C
            test_bit(&BC.low, 4, &F);
            break;
        case 0x62: // BIT 4,D
            test_bit(&DE.high, 4, &F);
            break;
        case 0x63: // BIT 4,E
            test_bit(&DE.low, 4, &F);
            break;
        case 0x64: // BIT 4,H
            test_bit(&HL.high, 4, &F);
            break;
        case 0x65: // BIT 4,L
            test_bit(&HL.low, 4, &F);
            break;
        case 0x66: // BIT 4,(HL)
            t_u8 = read(HL.full); tick();
            test_bit(&t_u8, 4, &F);
            break;
        case 0x67: // BIT 4,A
            test_bit(&A, 4, &F);
            break;
        case 0x68: // BIT 5,B
            test_bit(&BC.high, 5, &F);
            break;
        case 0x69: // BIT 5,C
            test_bit(&BC.low, 5, &F);
            break;
        case 0x6A: // BIT 5,D
            test_bit(&DE.high, 5, &F);
            break;
        case 0x6B: // BIT 5,E
            test_bit(&DE.low, 5, &F);
            break;
        case 0x6C: // BIT 5,H
            test_bit(&HL.high, 5, &F);
            break;
        case 0x6D: // BIT 5,L
            test_bit(&HL.low, 5, &F);
            break;
        case 0x6E: // BIT 5,(HL)
            t_u8 = read(HL.full); tick();
            test_bit(&t_u8, 5, &F);
            break;
        case 0x6F: // BIT 5,A
            test_bit(&A, 5, &F);
            break;
        case 0x70: // BIT 6,B
            test_bit(&BC.high, 6, &F);
            break;
        case 0x71: // BIT 6,C
            test_bit(&BC.low, 6, &F);
            break;
        case 0x72: // BIT 6,D
            test_bit(&DE.high, 6, &F);
            break;
        case 0x73: // BIT 6,E
            test_bit(&DE.low, 6, &F);
            break;
        case 0x74: // BIT 6,H
            test_bit(&HL.high, 6, &F);
            break;
        case 0x75: // BIT 6,L
            test_bit(&HL.low, 6, &F);
            break;
        case 0x76: // BIT 6,(HL)
            t_u8 = read(HL.full); tick();
            test_bit(&t_u8, 6, &F);
            break;
        case 0x77: // BIT 6,A
            test_bit(&A, 6, &F);
            break;
        case 0x78: // BIT 7,B
            test_bit(&BC.high, 7, &F);
            break;
        case 0x79: // BIT 7,C
            test_bit(&BC.low, 7, &F);
            break;
        case 0x7A: // BIT 7,D
            test_bit(&DE.high, 7, &F);
            break;
        case 0x7B: // BIT 7,E
            test_bit(&DE.low, 7, &F);
            break;
        case 0x7C: // BIT 7,H
            test_bit(&HL.high, 7, &F);
            break;
        case 0x7D: // BIT 7,L
            test_bit(&HL.low, 7, &F);
            break;
        case 0x7E: // BIT 7,(HL)
            t_u8 = read(HL.full); tick();
            test_bit(&t_u8, 7, &F);
            break;
        case 0x7F: // BIT 7,A
            test_bit(&A, 7, &F);
            break;
        case 0x80: // RES 0,B
            RESET_BIT(BC.high, 0);
            break;
        case 0x81: // RES 0,C
            RESET_BIT(BC.low, 0);
            break;
        case 0x82: // RES 0,D
            RESET_BIT(DE.high, 0);
            break;
        case 0x83: // RES 0,E
            RESET_BIT(DE.low, 0);
            break;
        case 0x84: // RES 0,H
            RESET_BIT(HL.high, 0);
            break;
        case 0x85: // RES 0,L
            RESET_BIT(HL.low, 0);
            break;
        case 0x86: // RES 0,(HL)
            t_u8 = read(HL.full); tick();
            RESET_BIT(t_u8, 0);
            write(HL.full, t_u8);
            break;
        case 0x87: // RES 0,A
            RESET_BIT(A, 0);
            break;
        case 0x88: // RES 1,B
            RESET_BIT(BC.high, 1);
            break;
        case 0x89: // RES 1,C
            RESET_BIT(BC.low, 1);
            break;
        case 0x8A: // RES 1,D
            RESET_BIT(DE.high, 1);
            break;
        case 0x8B: // RES 1,E
            RESET_BIT(DE.low, 1);
            break;
        case 0x8C: // RES 1,H
            RESET_BIT(HL.high, 1);
            break;
        case 0x8D: // RES 1,L
            RESET_BIT(HL.low, 1);
            break;
        case 0x8E: // RES 1,(HL)
            t_u8 = read(HL.full); tick();
            RESET_BIT(t_u8, 1);
            write(HL.full, t_u8);
            break;
        case 0x8F: // RES 1,A
            RESET_BIT(A, 1);
            break;
        case 0x90: // RES 2,B
            RESET_BIT(BC.high, 2);
            break;
        case 0x91: // RES 2,C
            RESET_BIT(BC.low, 2);
            break;
        case 0x92: // RES 2,D
            RESET_BIT(DE.high, 2);
            break;
        case 0x93: // RES 2,E
            RESET_BIT(DE.low, 2);
            break;
        case 0x94: // RES 2,H
            RESET_BIT(HL.high, 2);
            break;
        case 0x95: // RES 2,L
            RESET_BIT(HL.low, 2);
            break;
        case 0x96: // RES 2,(HL)
            t_u8 = read(HL.full); tick();
            RESET_BIT(t_u8, 2);
            write(HL.full, t_u8);
            break;
        case 0x97: // RES 2,A
            RESET_BIT(A, 2);
            break;
        case 0x98: // RES 3,B
            RESET_BIT(BC.high, 3);
            break;
        case 0x99: // RES 3,C
            RESET_BIT(BC.low, 3);
            break;
        case 0x9A: // RES 3,D
            RESET_BIT(DE.high, 3);
            break;
        case 0x9B: // RES 3,E
            RESET_BIT(DE.low, 3);
            break;
        case 0x9C: // RES 3,H
            RESET_BIT(HL.high, 3);
            break;
        case 0x9D: // RES 3,L
            RESET_BIT(HL.low, 3);
            break;
        case 0x9E: // RES 3,(HL)
            t_u8 = read(HL.full); tick();
            RESET_BIT(t_u8, 3);
            write(HL.full, t_u8);
            break;
        case 0x9F: // RES 3,A
            RESET_BIT(A, 3);
            break;
        case 0xA0: // RES 4,B
            RESET_BIT(BC.high, 4);
            break;
        case 0xA1: // RES 4,C
            RESET_BIT(BC.low, 4);
            break;
        case 0xA2: // RES 4,D
            RESET_BIT(DE.high, 4);
            break;
        case 0xA3: // RES 4,E
            RESET_BIT(DE.low, 4);
            break;
        case 0xA4: // RES 4,H
            RESET_BIT(HL.high, 4);
            break;
        case 0xA5: // RES 4,L
            RESET_BIT(HL.low, 4);
            break;
        case 0xA6: // RES 4,(HL)
            t_u8 = read(HL.full); tick();
            RESET_BIT(t_u8, 4);
            write(HL.full, t_u8);
            break;
        case 0xA7: // RES 4,A
            RESET_BIT(A, 4);
            break;
        case 0xA8: // RES 5,B
            RESET_BIT(BC.high, 5);
            break;
        case 0xA9: // RES 5,C
            RESET_BIT(BC.low, 5);
            break;
        case 0xAA: // RES 5,D
            RESET_BIT(DE.high, 5);
            break;
        case 0xAB: // RES 5,E
            RESET_BIT(DE.low, 5);
            break;
        case 0xAC: // RES 5,H
            RESET_BIT(HL.high, 5);
            break;
        case 0xAD: // RES 5,L
            RESET_BIT(HL.low, 5);
            break;
        case 0xAE: // RES 5,(HL)
            t_u8 = read(HL.full); tick();
            RESET_BIT(t_u8, 5);
            write(HL.full, t_u8);
            break;
        case 0xAF: // RES 5,A
            RESET_BIT(A, 5);
            break;
        case 0xB0: // RES 6,B
            RESET_BIT(BC.high, 6);
            break;
        case 0xB1: // RES 6,C
            RESET_BIT(BC.low, 6);
            break;
        case 0xB2: // RES 6,D
            RESET_BIT(DE.high, 6);
            break;
        case 0xB3: // RES 6,E
            RESET_BIT(DE.low, 6);
            break;
        case 0xB4: // RES 6,H
            RESET_BIT(HL.high, 6);
            break;
        case 0xB5: // RES 6,L
            RESET_BIT(HL.low, 6);
            break;
        case 0xB6: // RES 6,(HL)
            t_u8 = read(HL.full); tick();
            RESET_BIT(t_u8, 6);
            write(HL.full, t_u8);
            break;
        case 0xB7: // RES 6,A
            RESET_BIT(A, 6);
            break;
        case 0xB8: // RES 7,B
            RESET_BIT(BC.high, 7);
            break;
        case 0xB9: // RES 7,C
            RESET_BIT(BC.low, 7);
            break;
        case 0xBA: // RES 7,D
            RESET_BIT(DE.high, 7);
            break;
        case 0xBB: // RES 7,E
            RESET_BIT(DE.low, 7);
            break;
        case 0xBC: // RES 7,H
            RESET_BIT(HL.high, 7);
            break;
        case 0xBD: // RES 7,L
            RESET_BIT(HL.low, 7);
            break;
        case 0xBE: // RES 7,(HL)
            t_u8 = read(HL.full); tick();
            RESET_BIT(t_u8, 7);
            write(HL.full, t_u8);
            break;
        case 0xBF: // RES 7,A
            RESET_BIT(A, 7);
            break;
        case 0xC0: // SET 0,B
            SET_BIT(BC.high, 0);
            break;
        case 0xC1: // SET 0,C
            SET_BIT(BC.low, 0);
            break;
        case 0xC2: // SET 0,D
            SET_BIT(DE.high, 0);
            break;
        case 0xC3: // SET 0,E
            SET_BIT(DE.low, 0);
            break;
        case 0xC4: // SET 0,H
            SET_BIT(HL.high, 0);
            break;
        case 0xC5: // SET 0,L
            SET_BIT(HL.low, 0);
            break;
        case 0xC6: // SET 0,(HL)
            t_u8 = read(HL.full); tick();
            SET_BIT(t_u8, 0);
            write(HL.full, t_u8);
            break;
        case 0xC7: // SET 0,A
            SET_BIT(A, 0);
            break;
        case 0xC8: // SET 1,B
            SET_BIT(BC.high, 1);
            break;
        case 0xC9: // SET 1,C
            SET_BIT(BC.low, 1);
            break;
        case 0xCA: // SET 1,D
            SET_BIT(DE.high, 1);
            break;
        case 0xCB: // SET 1,E
            SET_BIT(DE.low, 1);
            break;
        case 0xCC: // SET 1,H
            SET_BIT(HL.high, 1);
            break;
        case 0xCD: // SET 1,L
            SET_BIT(HL.low, 1);
            break;
        case 0xCE: // SET 1,(HL)
            t_u8 = read(HL.full); tick();
            SET_BIT(t_u8, 1);
            write(HL.full, t_u8);
            break;
        case 0xCF: // SET 1,A
            SET_BIT(A, 1);
            break;
        case 0xD0: // SET 2,B
            SET_BIT(BC.high, 2);
            break;
        case 0xD1: // SET 2,C
            SET_BIT(BC.low, 2);
            break;
        case 0xD2: // SET 2,D
            SET_BIT(DE.high, 2);
            break;
        case 0xD3: // SET 2,E
            SET_BIT(DE.low, 2);
            break;
        case 0xD4: // SET 2,H
            SET_BIT(HL.high, 2);
            break;
        case 0xD5: // SET 2,L
            SET_BIT(HL.low, 2);
            break;
        case 0xD6: // SET 2,(HL)
            t_u8 = read(HL.full); tick();
            SET_BIT(t_u8, 2);
            write(HL.full, t_u8);
            break;
        case 0xD7: // SET 2,A
            SET_BIT(A, 2);
            break;
        case 0xD8: // SET 3,B
            SET_BIT(BC.high, 3);
            break;
        case 0xD9: // SET 3,C
            SET_BIT(BC.low, 3);
            break;
        case 0xDA: // SET 3,D
            SET_BIT(DE.high, 3);
            break;
        case 0xDB: // SET 3,E
            SET_BIT(DE.low, 3);
            break;
        case 0xDC: // SET 3,H
            SET_BIT(HL.high, 3);
            break;
        case 0xDD: // SET 3,L
            SET_BIT(HL.low, 3);
            break;
        case 0xDE: // SET 3,(HL)
            t_u8 = read(HL.full); tick();
            SET_BIT(t_u8, 3);
            write(HL.full, t_u8);
            break;
        case 0xDF: // SET 3,A
            SET_BIT(A, 3);
            break;
        case 0xE0: // SET 4,B
            SET_BIT(BC.high, 4);
            break;
        case 0xE1: // SET 4,C
            SET_BIT(BC.low, 4);
            break;
        case 0xE2: // SET 4,D
            SET_BIT(DE.high, 4);
            break;
        case 0xE3: // SET 4,E
            SET_BIT(DE.low, 4);
            break;
        case 0xE4: // SET 4,H
            SET_BIT(HL.high, 4);
            break;
        case 0xE5: // SET 4,L
            SET_BIT(HL.low, 4);
            break;
        case 0xE6: // SET 4,(HL)
            t_u8 = read(HL.full); tick();
            SET_BIT(t_u8, 4);
            write(HL.full, t_u8);
            break;
        case 0xE7: // SET 4,A
            SET_BIT(A, 4);
            break;
        case 0xE8: // SET 5,B
            SET_BIT(BC.high, 5);
            break;
        case 0xE9: // SET 5,C
            SET_BIT(BC.low, 5);
            break;
        case 0xEA: // SET 5,D
            SET_BIT(DE.high, 5);
            break;
        case 0xEB: // SET 5,E
            SET_BIT(DE.low, 5);
            break;
        case 0xEC: // SET 5,H
            SET_BIT(HL.high, 5);
            break;
        case 0xED: // SET 5,L
            SET_BIT(HL.low, 5);
            break;
        case 0xEE: // SET 5,(HL)
            t_u8 = read(HL.full); tick();
            SET_BIT(t_u8, 5);
            write(HL.full, t_u8);
            break;
        case 0xEF: // SET 5,A
            SET_BIT(A, 5);
            break;
        case 0xF0: // SET 6,B
            SET_BIT(BC.high, 6);
            break;
        case 0xF1: // SET 6,C
            SET_BIT(BC.low, 6);
            break;
        case 0xF2: // SET 6,D
            SET_BIT(DE.high, 6);
            break;
        case 0xF3: // SET 6,E
            SET_BIT(DE.low, 6);
            break;
        case 0xF4: // SET 6,H
            SET_BIT(HL.high, 6);
            break;
        case 0xF5: // SET 6,L
            SET_BIT(HL.low, 6);
            break;
        case 0xF6: // SET 6,(HL)
            t_u8 = read(HL.full); tick();
            SET_BIT(t_u8, 6);
            write(HL.full, t_u8);
            break;
        case 0xF7: // SET 6,A
            SET_BIT(A, 6);
            break;
        case 0xF8: // SET 7,B
            SET_BIT(BC.high, 7);
            break;
        case 0xF9: // SET 7,C
            SET_BIT(BC.low, 7);
            break;
        case 0xFA: // SET 7,D
            SET_BIT(DE.high, 7);
            break;
        case 0xFB: // SET 7,E
            SET_BIT(DE.low, 7);
            break;
        case 0xFC: // SET 7,H
            SET_BIT(HL.high, 7);
            break;
        case 0xFD: // SET 7,L
            SET_BIT(HL.low, 7);
            break;
        case 0xFE: // SET 7,(HL)
            t_u8 = read(HL.full); tick();
            SET_BIT(t_u8, 7);
            write(HL.full, t_u8);
            break;
        case 0xFF: // SET 7,A
            SET_BIT(A, 7);
            break;
    }
    return cycles;
}
u8 execute_instruction(u8 op) {
    u8       cycles;
    s8       t_s8;
    u8       t_u8;
    BytePair t_u16;
    int      t_int;

    cycles = op_cycles_lut[op];

    switch (op) {  // [Z N H C]
        case 0x00: // NOP
            break;
        case 0x01: // LD BC,d16
            BC.low = read(PC++); tick();
            BC.high = read(PC++); tick();
            break;
        case 0x02: // LD (BC),A
            write(BC.full, A);
            break;
        case 0x03: // INC BC
            BC.full++;
            tick();
            break;
        case 0x04: // INC B
            inc_u8(&BC.high, &F);
            break;
        case 0x05: // DEC B
            dec_u8(&BC.high, &F);
            break;
        case 0x06: // LD B,d8
            BC.high = read(PC++); tick();
            break;
        case 0x07: // RLCA
            F.C = GET_BIT(A, 7);
            A = ROTATE_LEFT(A, 1, 8);
            F.Z = 0;
            F.N = 0;
            F.H = 0;
            break;
        case 0x08: // LD (a16),SP
            t_u16.low = read(PC++); tick();
            t_u16.high = read(PC++); tick();
            write(t_u16.full, SP.low);
            write(t_u16.full + 1, SP.high);
            break;
        case 0x09: // ADD HL,BC
            add_u16(&HL.full, BC.full, &F);
            tick();
            break;
        case 0x0A: // LD A,(BC)
            A = read(BC.full); tick();
            break;
        case 0x0B: // DEC BC
            BC.full--;
            tick();
            break;
        case 0x0C: // INC C
            inc_u8(&BC.low, &F);
            break;
        case 0x0D: // DEC C
            dec_u8(&BC.low, &F);
            break;
        case 0x0E: // LD C,d8
            BC.low = read(PC++); tick();
            break;
        case 0x0F: // RRCA
            F.C = GET_BIT(A, 0);
            A = ROTATE_RIGHT(A, 1, 8);
            F.Z = 0;
            F.N = 0;
            F.H = 0;
            break;
        case 0x10: // STOP 0
            t_u8 = 0;
            // TODO
            break;
        case 0x11: // LD DE,d16
            DE.low = read(PC++); tick();
            DE.high = read(PC++); tick();
            break;
        case 0x12: // LD (DE),A
            write(DE.full, A);
            break;
        case 0x13: // INC DE
            DE.full++;
            tick();
            break;
        case 0x14: // INC D
            inc_u8(&DE.high, &F);
            break;
        case 0x15: // DEC D
            dec_u8(&DE.high, &F);
            break;
        case 0x16: // LD D,d8
            DE.high = read(PC++); tick();
            break;
        case 0x17: // RLA
            t_u8 = F.C;
            F.C = GET_BIT(A, 7);
            A <<= 1;
            if (t_u8) SET_BIT(A, 0);
            F.Z = 0;
            F.N = 0;
            F.H = 0;
            break;
        case 0x18: // JR r8
            t_s8 = (s8)(read(PC++)); tick();
            PC += t_s8;
            tick();
            break;
        case 0x19: // ADD HL,DE
            add_u16(&HL.full, DE.full, &F);
            tick();
            break;
        case 0x1A: // LD A,(DE)
            A = read(DE.full); tick();
            break;
        case 0x1B: // DEC DE
            DE.full--;
            tick();
            break;
        case 0x1C: // INC E
            inc_u8(&DE.low, &F);
            break;
        case 0x1D: // DEC E
            dec_u8(&DE.low, &F);
            break;
        case 0x1E: // LD E,d8
            DE.low = read(PC++); tick();
            break;
        case 0x1F: // RRA
            t_u8 = F.C;
            F.C = GET_BIT(A, 0);
            A >>= 1;
            if (t_u8) SET_BIT(A, 7);
            F.Z = 0;
            F.N = 0;
            F.H = 0;
            break;
        case 0x20: // JR NZ,r8
            t_s8 = (s8)(read(PC++)); tick();
            if (!F.Z) {
                PC += t_s8;
                tick();
                cycles += 4; // additional cycles if action was taken
            }
            break;
        case 0x21: // LD HL,d16
            HL.low = read(PC++); tick();
            HL.high = read(PC++); tick();
            break;
        case 0x22: // LD (HL+),A
            write(HL.full++, A);
            break;
        case 0x23: // INC HL
            HL.full++;
            tick();
            break;
        case 0x24: // INC H
            inc_u8(&HL.high, &F);
            break;
        case 0x25: // DEC H
            dec_u8(&HL.high, &F);
            break;
        case 0x26: // LD H,d8
            HL.high = read(PC++); tick();
            break;
        case 0x27: // DAA
            if (F.N == 0) {
                // after an addition, adjust if (half-)carry occurred or if result is out of bounds
                if (F.C || A > 0x99) {
                    A += 0x60; 
                    F.C = 1;
                } // upper nibble
                if (F.H || ((A & 0xF) > 0x9)) {
                    A += 0x6;
                }  // lower nibble
            }
            else {
                // after a subtraction, only adjust if (half-)carry occurred
                if (F.C) A -= 0x60; // upper nibble
                if (F.H) A -= 0x6;  // lower nibble
            }
            F.Z = (A == 0);
            F.H = 0;
            break;
        case 0x28: // JR Z,r8
            t_s8 = (s8)(read(PC++)); tick();
            if (F.Z) {
                PC += t_s8;
                tick();
                cycles += 4; // additional cycles if action was taken
            }
            break;
        case 0x29: // ADD HL,HL
            add_u16(&HL.full, HL.full, &F);
            tick();
            break;
        case 0x2A: // LD A,(HL+)
            A = read(HL.full++); tick();
            break;
        case 0x2B: // DEC HL
            HL.full--;
            tick();
            break;
        case 0x2C: // INC L
            inc_u8(&HL.low, &F);
            break;
        case 0x2D: // DEC L
            dec_u8(&HL.low, &F);
            break;
        case 0x2E: // LD L,d8
            HL.low = read(PC++); tick();
            break;
        case 0x2F: // CPL
            A ^= 0xFF; // flip bits
            F.N = 1;
            F.H = 1;
            break;
        case 0x30: // JR NC,r8
            t_s8 = (s8)(read(PC++)); tick();
            if (!F.C) {
                PC += t_s8;
                tick();
                cycles += 4;
            }
            break;
        case 0x31: // LD SP,d16
            SP.low = read(PC++); tick();
            SP.high = read(PC++); tick();
            break;
        case 0x32: // LD (HL-),A
            write(HL.full--, A);
            break;
        case 0x33: // INC SP
            SP.full++;
            tick();
            break;
        case 0x34: // INC (HL)
            t_u8 = read(HL.full); tick();
            inc_u8(&t_u8, &F);
            write(HL.full, t_u8);
            break;
        case 0x35: // DEC (HL)
            t_u8 = read(HL.full); tick();
            dec_u8(&t_u8, &F);
            write(HL.full, t_u8);
            break;
        case 0x36: // LD (HL),d8
            t_u8 = read(PC++); tick();
            write(HL.full, t_u8);
            break;
        case 0x37: // SCF
            F.C = 1;
            F.H = 0;
            F.N = 0;
            break;
        case 0x38: // JR C,r8
            t_s8 = (s8)(read(PC++)); tick();
            if (F.C) {
                PC += t_s8;
                tick();
                cycles += 4; // additional cycles if action was taken
            }
            break;
        case 0x39: // ADD HL,SP
            add_u16(&HL.full, SP.full, &F);
            tick();
            break;
        case 0x3A: // LD A,(HL-)
            A = read(HL.full--); tick();
            break;
        case 0x3B: // DEC SP
            SP.full--;
            tick();
            break;
        case 0x3C: // INC A
            inc_u8(&A, &F);
            break;
        case 0x3D: // DEC A
            dec_u8(&A, &F);
            break;
        case 0x3E: // LD A,d8
            A = read(PC++); tick();
            break;
        case 0x3F: // CCF
            F.C ^= 1;
            F.H = 0;
            F.N = 0;
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
            BC.high = read(HL.full); tick();
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
            BC.low = read(HL.full); tick();
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
            DE.high = read(HL.full); tick();
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
            DE.low = read(HL.full); tick();
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
            HL.high = read(HL.full); tick();
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
            HL.low = read(HL.full); tick();
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
            halted = 1;
            //printf("CPU halted\n");
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
            A = read(HL.full); tick();
            break;
        case 0x7F: // LD A,A
            //A = A;
            break;
        case 0x80: // ADD A,B
            add_u8(&A, BC.high, &F);
            break;
        case 0x81: // ADD A,C
            add_u8(&A, BC.low, &F);
            break;
        case 0x82: // ADD A,D
            add_u8(&A, DE.high, &F);
            break;
        case 0x83: // ADD A,E
            add_u8(&A, DE.low, &F);
            break;
        case 0x84: // ADD A,H
            add_u8(&A, HL.high, &F);
            break;
        case 0x85: // ADD A,L
            add_u8(&A, HL.low, &F);
            break;
        case 0x86: // ADD A,(HL)
            t_u8 = read(HL.full); tick();
            add_u8(&A, t_u8, &F);
            break;
        case 0x87: // ADD A,A
            add_u8(&A, A, &F);
            break;
        case 0x88: // ADC A,B
            adc_u8(&A, BC.high, &F);
            break;
        case 0x89: // ADC A,C
            adc_u8(&A, BC.low, &F);
            break;
        case 0x8A: // ADC A,D
            adc_u8(&A, DE.high, &F);
            break;
        case 0x8B: // ADC A,E
            adc_u8(&A, DE.low, &F);
            break;
        case 0x8C: // ADC A,H
            adc_u8(&A, HL.high, &F);
            break;
        case 0x8D: // ADC A,L
            adc_u8(&A, HL.low, &F);
            break;
        case 0x8E: // ADC A,(HL)
            t_u8 = read(HL.full); tick();
            adc_u8(&A, t_u8, &F);
            break;
        case 0x8F: // ADC A,A
            adc_u8(&A, A, &F);
            break;
        case 0x90: // SUB B
            sub_u8(&A, BC.high, &F);
            break;
        case 0x91: // SUB C
            sub_u8(&A, BC.low, &F);
            break;
        case 0x92: // SUB D
            sub_u8(&A, DE.high, &F);
            break;
        case 0x93: // SUB E
            sub_u8(&A, DE.low, &F);
            break;
        case 0x94: // SUB H
            sub_u8(&A, HL.high, &F);
            break;
        case 0x95: // SUB L
            sub_u8(&A, HL.low, &F);
            break;
        case 0x96: // SUB (HL)
            t_u8 = read(HL.full); tick();
            sub_u8(&A, t_u8, &F);
            break;
        case 0x97: // SUB A
            sub_u8(&A, A, &F);
            break;
        case 0x98: // SBC B
            sbc_u8(&A, BC.high, &F);
            break;
        case 0x99: // SBC C
            sbc_u8(&A, BC.low, &F);
            break;
        case 0x9A: // SBC D
            sbc_u8(&A, DE.high, &F);
            break;
        case 0x9B: // SBC E
            sbc_u8(&A, DE.low, &F);
            break;
        case 0x9C: // SBC H
            sbc_u8(&A, HL.high, &F);
            break;
        case 0x9D: // SBC L
            sbc_u8(&A, HL.low, &F);
            break;
        case 0x9E: // SBC (HL)
            t_u8 = read(HL.full); tick();
            sbc_u8(&A, t_u8, &F);
            break;
        case 0x9F: // SBC A
            sbc_u8(&A, A, &F);
            break;
        case 0xA0: // AND B
            and_u8(&A, BC.high, &F);
            break;
        case 0xA1: // AND C
            and_u8(&A, BC.low, &F);
            break;
        case 0xA2: // AND D
            and_u8(&A, DE.high, &F);
            break;
        case 0xA3: // AND E
            and_u8(&A, DE.low, &F);
            break;
        case 0xA4: // AND H
            and_u8(&A, HL.high, &F);
            break;
        case 0xA5: // AND L
            and_u8(&A, HL.low, &F);
            break;
        case 0xA6: // AND (HL)
            t_u8 = read(HL.full); tick();
            and_u8(&A, t_u8, &F);
            break;
        case 0xA7: // AND A
            and_u8(&A, A, &F);
            break;
        case 0xA8: // XOR B
            xor_u8(&A, BC.high, &F);
            break;
        case 0xA9: // XOR C
            xor_u8(&A, BC.low, &F);
            break;
        case 0xAA: // XOR D
            xor_u8(&A, DE.high, &F);
            break;
        case 0xAB: // XOR E
            xor_u8(&A, DE.low, &F);
            break;
        case 0xAC: // XOR H
            xor_u8(&A, HL.high, &F);
            break;
        case 0xAD: // XOR L
            xor_u8(&A, HL.low, &F);
            break;
        case 0xAE: // XOR (HL)
            t_u8 = read(HL.full); tick();
            xor_u8(&A, t_u8, &F);
            break;
        case 0xAF: // XOR A
            xor_u8(&A, A, &F);
            break;
        case 0xB0: // OR B
            or_u8(&A, BC.high, &F);
            break;
        case 0xB1: // OR C
            or_u8(&A, BC.low, &F);
            break;
        case 0xB2: // OR D
            or_u8(&A, DE.high, &F);
            break;
        case 0xB3: // OR E
            or_u8(&A, DE.low, &F);
            break;
        case 0xB4: // OR H
            or_u8(&A, HL.high, &F);
            break;
        case 0xB5: // OR L
            or_u8(&A, HL.low, &F);
            break;
        case 0xB6: // OR (HL)
            t_u8 = read(HL.full); tick();
            or_u8(&A, t_u8, &F);
            break;
        case 0xB7: // OR A
            or_u8(&A, A, &F);
            break;
        case 0xB8: // CP B
            cp_u8(&A, BC.high, &F);
            break;
        case 0xB9: // CP C
            cp_u8(&A, BC.low, &F);
            break;
        case 0xBA: // CP D
            cp_u8(&A, DE.high, &F);
            break;
        case 0xBB: // CP E
            cp_u8(&A, DE.low, &F);
            break;
        case 0xBC: // CP H
            cp_u8(&A, HL.high, &F);
            break;
        case 0xBD: // CP L
            cp_u8(&A, HL.low, &F);
            break;
        case 0xBE: // CP (HL)
            t_u8 = read(HL.full); tick();
            cp_u8(&A, t_u8, &F);
            break;
        case 0xBF: // CP A
            cp_u8(&A, A, &F);
            break;
        case 0xC0: // RET NZ
            tick();
            // Pop 2 bytes from the stack and increase SP (stack grows downwards)
            if (!F.Z) {
                t_u16.low = read(SP.full++); tick();
                t_u16.high = read(SP.full++); tick();
                PC = t_u16.full;
                tick();
                cycles += 12;
            }
            break;
        case 0xC1: // POP BC
            BC.low = read(SP.full++); tick();
            BC.high = read(SP.full++); tick();
            break;
        case 0xC2: // JP NZ,a16
            t_u16.low = read(PC++); tick();
            t_u16.high = read(PC++); tick();
            if (!F.Z) {
                PC = t_u16.full;
                tick();
                cycles += 4;
            }
            break;
        case 0xC3: // JP a16
            t_u16.low = read(PC++); tick();
            t_u16.high = read(PC++); tick();
            PC = t_u16.full;
            tick();
            break;
        case 0xC4: // CALL NZ,a16
            t_u16.low = read(PC++); tick();
            t_u16.high = read(PC++); tick();
            if (!F.Z) {
                // push PC onto stack, then jump to address
                write(--SP.full, (PC >> 8) & 0xFF);
                write(--SP.full, (PC & 0xFF));
                // jump to a16
                PC = t_u16.full;
                tick();
                cycles += 12;
            }
            break;
        case 0xC5: // PUSH BC
            tick();
            write(--SP.full, BC.high);
            write(--SP.full, BC.low);
            break;
        case 0xC6: // ADD A,d8
            t_u8 = read(PC++); tick();
            add_u8(&A, t_u8, &F);
            break;
        case 0xC7: // RST 00H
            tick();
            // push PC onto stack, then jump to address
            write(--SP.full, (PC >> 8) & 0xFF);
            write(--SP.full, (PC & 0xFF));
            PC = 0x0000;
            break;
        case 0xC8: // RET Z
            tick();
            // Pop 2 bytes from the stack and increase SP (stack grows downwards)
            if (F.Z) {
                t_u16.low = read(SP.full++); tick();
                t_u16.high = read(SP.full++); tick();
                PC = t_u16.full;
                tick();
                cycles += 12;
            }
            break;
        case 0xC9: // RET
            t_u16.low = read(SP.full++); tick();
            t_u16.high = read(SP.full++); tick();
            PC = t_u16.full;
            tick();
            break;
        case 0xCA: // JP Z,a16
            t_u16.low = read(PC++); tick();
            t_u16.high = read(PC++); tick();
            if (F.Z) {
                PC = t_u16.full;
                tick();
                cycles += 4;
            }
            break;
        case 0xCB: // Prefix CB
            t_u8 = read(PC++); tick();
            cycles = execute_cb(t_u8);
            break;
        case 0xCC: // CALL Z,a16
            t_u16.low = read(PC++); tick();
            t_u16.high = read(PC++); tick();
            if (F.Z) {
                // push PC onto stack, then jump to address
                write(--SP.full, (PC >> 8) & 0xFF);
                write(--SP.full, (PC & 0xFF));
                PC = t_u16.full;
                tick();
                cycles += 12;
            }
            break;
        case 0xCD: // CALL a16
            t_u16.low = read(PC++); tick();
            t_u16.high = read(PC++); tick();
            // push PC onto stack, then jump to address
            write(--SP.full, (PC >> 8) & 0xFF);
            write(--SP.full, (PC & 0xFF));
            PC = t_u16.full;
            tick();
            break;
        case 0xCE: // ADC A,d8
            t_u8 = read(PC++); tick();
            adc_u8(&A, t_u8, &F);
            break;
        case 0xCF: // RST 08H
            tick();
            // push PC onto stack, then jump to address
            write(--SP.full, (PC >> 8) & 0xFF);
            write(--SP.full, (PC & 0xFF));
            PC = 0x0008;
            break;

        case 0xD0: // RET NC
            tick();
            if (!F.C) {
                // Pop 2 bytes from the stack and increase SP (stack grows downwards)
                t_u16.low = read(SP.full++); tick();
                t_u16.high = read(SP.full++); tick();
                PC = t_u16.full;
                tick();
                cycles += 12;
            }
            break;
        case 0xD1: // POP DE
            DE.low = read(SP.full++); tick();
            DE.high = read(SP.full++); tick();
            break;
        case 0xD2: // JP NC,a16
            t_u16.low = read(PC++); tick();
            t_u16.high = read(PC++); tick();
            if (!F.C) {
                PC = t_u16.full;
                tick();
                cycles += 4;
            }
            break;
        case 0xD3:
            // nothing here
            break;
        case 0xD4: // CALL NC,a16
            t_u16.low = read(PC++); tick();
            t_u16.high = read(PC++); tick();
            if (!F.C) {
                // push PC onto stack, then jump to address
                write(--SP.full, (PC >> 8) & 0xFF);
                write(--SP.full, (PC & 0xFF));
                PC = t_u16.full;
                tick();
                cycles += 12;
            }
            break;
        case 0xD5: // PUSH DE
            tick();
            write(--SP.full, DE.high);
            write(--SP.full, DE.low);
            break;
        case 0xD6: // SUB d8
            t_u8 = read(PC++); tick();
            sub_u8(&A, t_u8, &F);
            break;
        case 0xD7: // RST 10H
            tick();
            // push PC onto stack, then jump to address
            write(--SP.full, (PC >> 8) & 0xFF);
            write(--SP.full, (PC & 0xFF));
            PC = 0x0010;
            break;
        case 0xD8: // RET C
            tick();
            if (F.C) {
                // Pop 2 bytes from the stack and increase SP (stack grows downwards)
                t_u16.low = read(SP.full++); tick();
                t_u16.high = read(SP.full++); tick();
                PC = t_u16.full;
                tick();
                cycles += 12;
            }
            break;
        case 0xD9: // RETI
            t_u16.low = read(SP.full++); tick();
            t_u16.high = read(SP.full++); tick();
            PC = t_u16.full;
            tick();
            interrupts_enabled = 1;
            break;
        case 0xDA: // JP C,a16
            t_u16.low = read(PC++); tick();
            t_u16.high = read(PC++); tick();
            if (F.C) {
                PC = t_u16.full;
                tick();
                cycles += 4;
            }
            break;
        case 0xDB:
            // nothing here
            break;
        case 0xDC: // CALL C,a16
            t_u16.low = read(PC++); tick();
            t_u16.high = read(PC++); tick();
            if (F.C) {
                // push PC onto stack, then jump to address
                write(--SP.full, (PC >> 8) & 0xFF);
                write(--SP.full, (PC & 0xFF));
                PC = t_u16.full;
                tick();
                cycles += 12;
            }
            break;
        case 0xDD:
            // nothing here
            break;
        case 0xDE: // SBC A,d8
            t_u8 = read(PC++); tick();
            sbc_u8(&A, t_u8, &F);
            break;
        case 0xDF: // RST 18H
            tick();
            // push PC onto stack, then jump to address
            write(--SP.full, (PC >> 8) & 0xFF);
            write(--SP.full, (PC & 0xFF));
            PC = 0x0018;
            break;

        case 0xE0: // LDH (a8),A
            // Put A into memory address 0xFF00+n (IO)
            t_u8 = read(PC++); tick();
            write(MEM_IO + t_u8, A);
            break;
        case 0xE1: // POP HL
            HL.low = read(SP.full++); tick();
            HL.high = read(SP.full++); tick();
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
            tick();
            write(--SP.full, HL.high);
            write(--SP.full, HL.low);
            break;
        case 0xE6: // AND d8
            t_u8 = read(PC++); tick();
            and_u8(&A, t_u8, &F);
            break;
        case 0xE7: // RST 20H
            tick();
            // push PC onto stack, then jump to address
            write(--SP.full, (PC >> 8) & 0xFF);
            write(--SP.full, (PC & 0xFF));
            PC = 0x0020;
            break;
        case 0xE8: // ADD SP,r8
            t_s8 = (s8)read(PC++); tick();
            t_int = SP.full + t_s8;
            
            F.N = 0;
            F.Z = 0;
            // Set Half-Carry flag if bit 4 changed due to addition
            F.H = (((SP.full ^ t_s8 ^ (t_int & 0xFFFF)) & 0x10) == 0x10);
            // Set Carry flag if bit 8 changed due to addition
            F.C = (((SP.full ^ t_s8 ^ (t_int & 0xFFFF)) & 0x100) == 0x100);

            //SP.full += t_s8;
            SP.low = (t_int & 0xFFFF) & 0xFF;
            tick();
            SP.high = ((t_int & 0xFFFF) >> 8) & 0xFF;
            tick();
            break;
        case 0xE9: // JP (HL)
            PC = HL.full;
            break;
        case 0xEA: // LD (a16),A
            t_u16.low = read(PC++); tick();
            t_u16.high = read(PC++); tick();
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
            t_u8 = read(PC++); tick();
            xor_u8(&A, t_u8, &F);
            break;
        case 0xEF: // RST 28H
            tick();
            // push PC onto stack, then jump to address
            write(--SP.full, (PC >> 8) & 0xFF);
            write(--SP.full, (PC & 0xFF));
            PC = 0x0028;
            break;

        case 0xF0: // LDH A,(a8)
            
            // Put value in memory address 0xFF00+n into A
            t_u8 = read(PC++); tick();
            A = read(MEM_IO + t_u8);
            tick();
            break;
        case 0xF1: // POP AF
            t_u8 = read(SP.full++); tick();
            F.C = GET_BIT(t_u8, 4);
            F.H = GET_BIT(t_u8, 5);
            F.N = GET_BIT(t_u8, 6);
            F.Z = GET_BIT(t_u8, 7);
            A = read(SP.full++); tick();
            break;
        case 0xF2: // LD A,(C)
            
            // Put value in memory address 0xFF00+C into A
            A = read(MEM_IO + BC.low); tick();
            break;
        case 0xF3: // DI
            if (ei_flag) ei_flag = 0;
            interrupts_enabled = 0;
            break;
        case 0xF4:
            // nothing here
            break;
        case 0xF5: // PUSH AF
            tick();
            write(--SP.full, A);
            // reconstruct the F register
            t_u8 = 0;
            t_u8 |= ((F.Z << 7) | (F.N << 6) | (F.H << 5) | (F.C << 4));
            write(--SP.full, t_u8);
            break;
        case 0xF6: // OR d8
            t_u8 = read(PC++); tick();
            or_u8(&A, t_u8, &F);
            break;
        case 0xF7: // RST 30H
            tick();
            // push PC onto stack, then jump to address
            write(--SP.full, (PC >> 8) & 0xFF);
            write(--SP.full, (PC & 0xFF));
            PC = 0x0030;
            break;
        case 0xF8: // LD HL,SP+r8
            t_s8 = (s8)read(PC++); tick();
            t_int = SP.full + t_s8;

            F.N = 0;
            F.Z = 0;
            // Set Half-Carry flag if bit 4 changed due to addition
            F.H = (((SP.full ^ t_s8 ^ (t_int & 0xFFFF)) & 0x10) == 0x10);
            // Set Carry flag if bit 8 changed due to addition
            F.C = (((SP.full ^ t_s8 ^ (t_int & 0xFFFF)) & 0x100) == 0x100);

            HL.full = (SP.full + t_s8);
            tick();
            break;
        case 0xF9: // LD SP,HL
            SP.full = HL.full;
            tick();
            break;
        case 0xFA: // LD A,(a16)
            t_u16.low = read(PC++); tick();
            t_u16.high = read(PC++); tick();
            A = read(t_u16.full); tick();
            break;
        case 0xFB: // EI
            ei_flag = 1;
            break;
        case 0xFC:
            // nothing here
            break;
        case 0xFD:
            // nothing here
            break;
        case 0xFE: // CP d8
            t_u8 = read(PC++); tick();
            cp_u8(&A, t_u8, &F);
            break;
        case 0xFF: // RST 38H
            tick();
            // push PC onto stack, then jump to address
            write(--SP.full, (PC >> 8) & 0xFF);
            write(--SP.full, (PC & 0xFF));
            PC = 0x0038;
            break;
    }
    return cycles;
}
