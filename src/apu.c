#include "apu.h"
#include "macros.h"

#include <stdio.h>
#include <string.h>
#include "emu_shared.h"

u8 apu_enabled; // copy of register NR52 bit 7 
u8 apu_div;

u8 vol_l, vol_r;             // master volume   0-7 range
u8 vin_pan_l, vin_pan_r;     // VIN panning     0/1 flag
u8 ch_pan_l[4], ch_pan_r[4]; // sound panning   0/1 flag
u8 ch_enabled[4];            // whether a sound channel is enabled or not
u8 ch_dac_enabled[4];        // whether a sound channel's DAC is enabled or not

u8 ch1_sweep_pace;           // how quickly the sweep period gets changed over time 0-7 range
u8 ch1_sweep_negative;       // sweep increase/decrease 0/1 flag
u8 ch1_sweep_slope;          // how big the sweep period change is each iteration

u8 ch1_len_timer_start;      // the higher this value is, the shorter the time before the channel is cut (it counts up to 64)
u8 ch1_duty_cycle;           // the ratio of the time spent low vs. high

u8 ch1_env_sweep_pace;       // how quickly the volume of the sound changes over time (0=no Sweep)
u8 ch1_env_positive;         // envelope direction (0=decrease, 1=increase)
u8 ch1_env_vol_start;        // initial volume of envelope (0-F) (0=no Sound)

u16 ch1_period_value;        // combination of NR13 + NR14(0-2)
u8 ch1_len_enabled;          // 1=Stop output when length in NR11 expires

// PUBLIC   -------------------------------------------------

u8 apu_read_register(u8 reg_id) {
    switch (reg_id) {
        case REG_NR11:
            return (reg[REG_NR11] | 0x3F); // bits 0~5 are write only (masked with 0b 0011 1111)
        case REG_NR13:
            return 0xFF; // write only
        case REG_NR14:
            return (reg[REG_NR14] | 0xBF); // only bit 6 is readable (masked with 0b 1011 1111)
        default:
            return reg[reg_id];
    }
}

int apu_write_register(u8 reg_id, u8 value) {
    // Turning the APU off makes its registers read-only until turned back on, except NR52
    if (!apu_enabled && reg_id != REG_NR52) return -1;

    switch (reg_id) {
        case REG_NR10:
            /*
            Bit 6-4 - Sweep pace
            Bit 3   - Sweep increase/decrease (0: Addition (period increases) 1: Subtraction (period decreases))
            Bit 2-0 - Sweep slope control (n: 0-7)
            */
            ch1_sweep_pace      = (value >> 4) & 0x7;
            ch1_sweep_negative  = GET_BIT(value, 3);
            ch1_sweep_slope     = value & 0x7;

            // TODO The pace is only reloaded after the following sweep iteration, or when (re)triggering the channel. 
            // However, if bits 4–6 are all set to 0, then iterations are instantly disabled, 
            // and the pace will be reloaded immediately if it’s set to something else.

            reg[reg_id] = value;
            break;
        case REG_NR11:
            /*
            Bit 7-6 - Wave duty            (Read/Write)
            Bit 5-0 - Initial length timer (Write Only)
            */
            ch1_len_timer_start = value & 0x3F;
            ch1_duty_cycle      = (value >> 6) & 0x3;

            reg[reg_id] = value;
            break;
        case REG_NR12:
            /*
            Bit 7-4 - Initial volume of envelope (0-F) (0=No Sound)
            Bit 3   - Envelope direction (0=Decrease, 1=Increase)
            Bit 2-0 - Sweep pace (0=No Sweep)
            */

            // Setting bits 3-7 of this register all to 0 turns the DAC off, which also turns the channel off
            ch_dac_enabled[0] = ((value & 0xF8) != 0);
            if (!ch_dac_enabled[0]) ch_enabled[0] = 0; // channel can only turn off by the DAC, it can't turn it on
            
            // Writes to this register while the channel is on require retriggering it afterwards.
            if (!ch_enabled[0]) {
                ch1_env_sweep_pace = value & 0x7;
                ch1_env_positive = GET_BIT(value, 3);
                ch1_env_vol_start = (value >> 4) & 0xF;
            }

            reg[reg_id] = value;
            break;
        case REG_NR13:
            // This registers stores the lower 8 bits of channel’s 11-bit “period value”
            ch1_period_value = (ch1_period_value & 0xFF00) | (value & 0xFF);

            reg[reg_id] = value;
            break;
        case REG_NR14:
            /*
            Bit 7   - Trigger (1=Restart channel)  (Write Only)
            Bit 6   - Sound Length enable          (Read/Write) (1=Stop output when length in NR11 expires)
            Bit 2-0 - Period value's higher 3 bits (Write Only)
            */
            ch1_period_value    = (ch1_period_value & 0x00FF) | ( ((u16)(value & 0x7) << 8) & 0xFFFF );
            ch1_len_enabled     = GET_BIT(value, 6);
            // trigger the channel
            if (GET_BIT(value, 7)) {
                if (ch_dac_enabled[0]) {
                    ch_enabled[0] = 1;
                    // activate parameters that required re-triggering to take effect
                    ch1_env_sweep_pace  = reg[REG_NR12] & 0x7;
                    ch1_env_positive    = GET_BIT(reg[REG_NR12], 3);
                    ch1_env_vol_start   = (reg[REG_NR12] >> 4) & 0xF;
                }
            }
            reg[reg_id] = value;
            break;

        case REG_NR50:
            /*
            Bit 7   - Mix VIN into left output  (1=Enable)
            Bit 6-4 - Left output volume        (0-7)
            Bit 3   - Mix VIN into right output (1=Enable)
            Bit 2-0 - Right output volume       (0-7)
            */
            vol_r       = value & 0x7;       
            vol_l       = (value >> 4) & 0x7;
            vin_pan_r   = GET_BIT(value, 3);
            vin_pan_l   = GET_BIT(value, 7);

            reg[reg_id] = value;
            break;
        case REG_NR51:
            /*
            Bit 7 - Mix channel 4 into left output
            Bit 6 - Mix channel 3 into left output
            Bit 5 - Mix channel 2 into left output
            Bit 4 - Mix channel 1 into left output
            Bit 3 - Mix channel 4 into right output
            Bit 2 - Mix channel 3 into right output
            Bit 1 - Mix channel 2 into right output
            Bit 0 - Mix channel 1 into right output
            */
            for (int i = 0; i < 4; i++) {
                ch_pan_r[i] = GET_BIT(value, i);
                ch_pan_l[i] = GET_BIT(value, i + 4);
            }
            reg[reg_id] = value;
            break;
        case REG_NR52:
            /*
            Bit 7 - All sound on/off  (0: turn the APU off) (Read/Write)
            Rest are read only / not used
            */
            // Turn off APU
            if (GET_BIT(reg[REG_NR52], 7) && !GET_BIT(value, 7)) {
                RESET_BIT(reg[REG_NR52], 7);
                apu_enabled = 0;
                turn_off();
            }
            // Turn on APU
            else if (!GET_BIT(reg[REG_NR52], 7) && GET_BIT(value, 7)) {
                SET_BIT(reg[REG_NR52], 7);
                apu_enabled = 1;
            }
            break;

        default:
            reg[reg_id] = value;
            break;
    }
    return 0;
}

apu_tick(u8 cycles) {
    u8 clock = cycles;
    /*
    A “DIV-APU” counter is increased every time DIV’s bit 4 (5 in double-speed mode) goes from 1 to 0, 
    therefore at a frequency of 512 Hz (regardless of whether double-speed is active). 
    Thus, the counter can be made to increase faster by writing to DIV while its relevant bit is set 
    (which clears DIV, and triggers the falling edge).
    */
    div_apu_counter += 4;

}



// PRIVATE  -------------------------------------------------

void turn_off() {
    reg[REG_NR10] = 0;
    reg[REG_NR11] = 0;
    reg[REG_NR12] = 0;
    reg[REG_NR13] = 0;
    reg[REG_NR14] = 0;
    reg[REG_NR21] = 0;
    reg[REG_NR22] = 0;
    reg[REG_NR23] = 0;
    reg[REG_NR24] = 0;
    reg[REG_NR30] = 0;
    reg[REG_NR31] = 0;
    reg[REG_NR32] = 0;
    reg[REG_NR33] = 0;
    reg[REG_NR34] = 0;
    reg[REG_NR41] = 0;
    reg[REG_NR42] = 0;
    reg[REG_NR43] = 0;
    reg[REG_NR44] = 0;
    reg[REG_NR50] = 0;
    reg[REG_NR51] = 0;

    reg[REG_WAVERAM] = 0;
}