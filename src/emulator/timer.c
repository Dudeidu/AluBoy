#include "timer.h"
#include "macros.h"

#include <stdio.h>
#include <string.h>
#include "emu_shared.h"

#include "apu.h" // for obscure behavior when writing to DIV


u8  timer_enabled;      // bit  2   of reg TAC
u16 timer_speed;        // bits 0-1 of reg TAC
u8  timer_clock_bit;    // Picks a bit from the internal clock and uses it to increase TIMA when detects a falling edge (1 to 0)
u8  tima_reload_delay = 0; // emulates a timer quirk: when the timer overflows, the TIMA register contains 00 for 4 cycles

// Forward declarations
void tima_inc();

void timer_init() {
}

void timer_powerup()
{
    internal_counter  = 0xABCC;
    reg[REG_DIV]    = (internal_counter >> 8) & 0xFF;
    reg[REG_TIMA]   = 0x00;
    reg[REG_TMA]    = 0x00;
    reg[REG_TAC]    = 0xF8;
    timer_speed     = 1024;
    timer_clock_bit = 9;
    timer_enabled   = 0;
}

u8 timer_read_register(u8 reg_id)
{
    // handles special cases
    switch (reg_id) {
        case REG_TIMA:
            return (tima_reload_delay > 0) ? 0x00 : reg[REG_TIMA];
        default:
            return reg[reg_id];
    }
}

void timer_write_register(u8 reg_id, u8 value)
{
    // handles special cases
    switch (reg_id) {
        case REG_DIV:
            // emulates the internal div counter's bits changing 
            // from high to low which in turn triggers a timer increment.
            if (timer_enabled && GET_BIT(internal_counter, timer_clock_bit)) {
                tima_inc();
            }
            // the DIV-APU counter can be made to increase faster by writing to DIV
            // while its relevant bit is set (which clears DIV, and triggers the falling edge).
            if (GET_BIT(internal_counter, apu_clock_bit)) {
                apu_frame_sequencer_update();
            }

            internal_counter = 0;
            reg[REG_DIV] = 0;
            break;
        case REG_TIMA:
            if (tima_reload_delay == 0) {
                reg[REG_TIMA] = value;
            }
            break;
        case REG_TMA:
            if (tima_reload_delay == 0) {
                reg[REG_TMA] = value;
            }
            break;
        case REG_TAC:
            // bit 0–1: Select at which frequency TIMA increases
            // More accurately it picks a bit from the internal clock and uses it to increase TIMA when it falls from 1 to 0
            switch (value & 0x3) {
                case 0: timer_speed = 1024; timer_clock_bit = 9; break;
                case 1: timer_speed = 16;   timer_clock_bit = 3; break;
                case 2: timer_speed = 64;   timer_clock_bit = 5; break;
                case 3: timer_speed = 256;  timer_clock_bit = 7;  break;
            }
            // bit 2: Enable timer
            // When disabling the timer, if the corresponding bit in the system counter is set to 1, the falling edge
            // detector will see a change from 1 to 0, so TIMA will increase.

            if (timer_enabled && !GET_BIT(value, 2)) {
                if (GET_BIT(internal_counter, timer_clock_bit)) {
                    tima_inc();
                }
            }
            timer_enabled = GET_BIT(value, 2);

            reg[REG_TAC] = value;
            break;
    }

}

void timer_tick() {
    u8 clock = (M_CYCLE >> double_speed);
    u8 timer_inc_check_old = timer_enabled & GET_BIT(internal_counter, timer_clock_bit);
    u8 timer_inc_check_new;

    // DIV is incremented at 16384Hz / 32768Hz in double speed
    internal_counter = (internal_counter + clock) & 0xFFFF;
    reg[REG_DIV] = (internal_counter >> 8) & 0xFF;

    // TIMA reload delay
    if (tima_reload_delay > 0) {
        if ((s8)tima_reload_delay - clock >= 0) tima_reload_delay -= clock;
        else {
            tima_reload_delay = 0;
        }
        // Timer interrupt
        if (tima_reload_delay == 0) {
            SET_BIT(reg[REG_IF], INT_BIT_TIMER);
        }
    }

    // TIMA is incremented when detecting a falling edge from ANDing the enable bit in TAC with the bit of the system internal counter
    timer_inc_check_new = timer_enabled & GET_BIT(internal_counter, timer_clock_bit);
    if (timer_inc_check_old && !timer_inc_check_new) {
        tima_inc();
    }
    //printf("DIV: %d, TIMA: %d\n", reg[REG_DIV], reg[REG_TIMA]);

}

void tima_inc() {
    u8 tima_old = reg[REG_TIMA];
    reg[REG_TIMA] = (tima_old + 1) & 0xFF;
    // if overflow occured
    if (reg[REG_TIMA] < tima_old) {
        reg[REG_TIMA] = (reg[REG_TIMA] + reg[REG_TMA]);
        tima_reload_delay = 4;
    }
}