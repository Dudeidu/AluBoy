#include "apu.h"
#include "macros.h"

#include <stdio.h>
#include <string.h>
#include "emu_shared.h"

extern void audio_add_sample(u8 sample);


u16 sample_frequency = 95;  // how often to gather samples 4194304 Hz / 44100 / Hz / 4 t-cycles
u16 sample_timer = 0;       // counts up to sample_frequency

u8 apu_enabled;             // copy of register NR52 bit 7 
u8 frame_sequencer;
u8 apu_clock_bit;           // Picks a bit from the internal clock and uses it to increase DIV-APU when detects a falling edge (1 to 0)

u8 vol_l, vol_r;            // master volume   0-7 range
u8 vin_pan_l, vin_pan_r;    // VIN panning     0/1 flag

u8 ch1_sweep_pace;          // how quickly the sweep period gets changed over time 0-7 range
u8 ch1_sweep_negative;      // sweep increase/decrease 0/1 flag
u8 ch1_sweep_slope;         // how big the sweep period change is each iteration

u8 ch1_duty_cycle;          // defines which waveform is used. the ratio of the time spent low vs. high
u8 ch1_sequence;            // the bit index in the current waveform

u8 ch1_env_sweep_pace;      // how quickly the volume of the sound changes over time (0=no Sweep)
u8 ch1_env_positive;        // envelope direction (0=decrease, 1=increase)
u8 ch1_env_vol;       // initial volume of envelope (0-F) (0=no Sound)
u8 ch1_env_sweep_counter;
u8 ch1_env_active;

u8 ch2_duty_cycle;          // defines which waveform is used. the ratio of the time spent low vs. high
u8 ch2_sequence;            // the bit index in the current waveform

u8 ch2_env_sweep_pace;      // how quickly the volume of the sound changes over time (0=no Sweep)
u8 ch2_env_positive;        // envelope direction (0=decrease, 1=increase)
u8 ch2_env_vol;       // initial volume of envelope (0-F) (0=no Sound)
u8 ch2_env_sweep_counter;
u8 ch2_env_active;

const u8 sw_duty_cycle[4] = {   // square channel duty cycle waveforms
    0x1,    // 12.5%    00000001
    0x81,   // 25.0%    10000001
    0x87,   // 50.0%    10000111
    0x7E    // 75.0%    01111110
};

typedef struct Channel {
    u8  enabled;        // whether a sound channel is enabled or not
    u8  dac_enabled;    // whether a sound channel's DAC is enabled or not
    u8  pan_left;       // sound panning   0/1 flag
    u8  pan_right;
    u8  output;         

    u8  len_enabled;    // 1=Stop output when length in NRX1 expires
    u8  len_timer;      // the higher this value is, the shorter the time before the channel is cut (it counts up to 64)
    u16 frequency;      // combination of NRX3 + NRX4(0-2)  
    int timer;          // the timer resets to ((2048 - frequency) * 4) when it reaches 0
} Channel;

Channel ch1;
Channel ch2;
Channel ch3;
Channel ch4;



// Forward declaration
void    turn_off();
u8      apu_read_register(u8 reg_id);
int     apu_write_register(u8 reg_id, u8 value);
void    update_length();
void    update_envelope();
void    update_ch1_sweep();

// PUBLIC   -------------------------------------------------

void apu_init() {
    apu_enabled     = 1;
    frame_sequencer = 0;
    apu_clock_bit   = 12;

    apu_write_register(REG_NR50, reg[REG_NR50]);
    apu_write_register(REG_NR51, reg[REG_NR51]);
    apu_write_register(REG_NR52, reg[REG_NR52]);

    ch1.dac_enabled = 1;
    ch1.enabled     = 1;
    ch1_env_sweep_counter = 0;
    ch1_env_active  = 1;
    apu_write_register(REG_NR10, reg[REG_NR10]);
    apu_write_register(REG_NR11, reg[REG_NR11]);
    apu_write_register(REG_NR12, reg[REG_NR12]);
    apu_write_register(REG_NR13, reg[REG_NR13]);
    apu_write_register(REG_NR14, reg[REG_NR14]);

    ch2.dac_enabled = 1;
    ch2.enabled     = 1;
    ch2_env_sweep_counter = 0;
    ch2_env_active  = 1;
    apu_write_register(REG_NR21, reg[REG_NR21]);
    apu_write_register(REG_NR22, reg[REG_NR22]);
    apu_write_register(REG_NR23, reg[REG_NR23]);
    apu_write_register(REG_NR24, reg[REG_NR24]);
    /*
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
    */

}

u8 apu_read_register(u8 reg_id) {
    switch (reg_id) {
        // Pulse/Square Channel 1
        case REG_NR11:
            return (reg[REG_NR11] | 0x3F); // bits 0~5 are write only (masked with 0b 0011 1111)
        case REG_NR13:
            return 0xFF; // write only
        case REG_NR14:
            return (reg[REG_NR14] | 0xBF); // only bit 6 is readable (masked with 0b 1011 1111)
        
        // Pulse/Square Channel 2
        case REG_NR21:
            return (reg[REG_NR21] | 0x3F); // bits 0~5 are write only (masked with 0b 0011 1111)
        case REG_NR23:
            return 0xFF; // write only
        case REG_NR24:
            return (reg[REG_NR24] | 0xBF); // only bit 6 is readable (masked with 0b 1011 1111)

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
            ch1_duty_cycle      = (value >> 6) & 0x3;
            ch1.len_timer       = value & 0x3F;

            reg[reg_id] = value;
            break;
        case REG_NR12:
            /*
            Bit 7-4 - Initial volume of envelope (0-F) (0=No Sound)
            Bit 3   - Envelope direction (0=Decrease, 1=Increase)
            Bit 2-0 - Volume Sweep pace (0=No Sweep)
            */

            // Setting bits 3-7 of this register all to 0 turns the DAC off, which also turns the channel off
            ch1.dac_enabled = ((value & 0xF8) != 0);
            if (!ch1.dac_enabled) ch1.enabled = 0; // channel can only turn off by the DAC, it can't turn it on
            
            // Writes to this register while the channel is on require retriggering it afterwards.
            if (!ch1.enabled) {
                ch1_env_sweep_pace = value & 0x7;
                ch1_env_positive = GET_BIT(value, 3);
                ch1_env_vol = (value >> 4) & 0xF;
            }

            reg[reg_id] = value;
            break;
        case REG_NR13:
            // This registers stores the lower 8 bits of channel’s 11-bit “period value”
            ch1.frequency = (ch1.frequency & 0xFF00) | (value & 0xFF);

            reg[reg_id] = value;
            break;
        case REG_NR14:
            /*
            Bit 7   - Trigger (1=Restart channel)  (Write Only)
            Bit 6   - Sound Length enable          (Read/Write) (1=Stop output when length in NR11 expires)
            Bit 2-0 - Period value's higher 3 bits (Write Only)
            */
            ch1.frequency  = (ch1.frequency & 0x00FF) | ( ((u16)(value & 0x7) << 8) & 0xFFFF );
            ch1.len_enabled = GET_BIT(value, 6);
            // trigger the channel
            if (GET_BIT(value, 7)) {
                ch1.enabled = 1;
                // If length counter is zero, it is set to 64 (256 for wave channel).
                if (ch1.len_timer == 64) ch1.len_timer = 0;
                ch1_env_active = 1;
                // activate parameters that required re-triggering to take effect
                ch1_env_sweep_pace  = reg[REG_NR12] & 0x7;
                ch1_env_positive    = GET_BIT(reg[REG_NR12], 3);
                ch1_env_vol   = (reg[REG_NR12] >> 4) & 0xF;

                ch1_env_sweep_counter = ch1_env_sweep_pace;
                ch1.timer = ((2048 - ch1.frequency) << 2);

                if (!ch1.dac_enabled)
                    ch1.enabled = 0;
            }
            reg[reg_id] = value;
            break;
        
        case REG_NR21:
            /*
            Bit 7-6 - Wave duty            (Read/Write)
            Bit 5-0 - Initial length timer (Write Only)
            */
            ch2_duty_cycle      = (value >> 6) & 0x3;
            ch2.len_timer       = value & 0x3F;

            reg[reg_id] = value;
            break;
        case REG_NR22:
            /*
            Bit 7-4 - Initial volume of envelope (0-F) (0=No Sound)
            Bit 3   - Envelope direction (0=Decrease, 1=Increase)
            Bit 2-0 - Sweep pace (0=No Sweep)
            */

            // Setting bits 3-7 of this register all to 0 turns the DAC off, which also turns the channel off
            ch2.dac_enabled = ((value & 0xF8) != 0);
            if (!ch2.dac_enabled) ch2.enabled = 0; // channel can only turn off by the DAC, it can't turn it on
            
            // Writes to this register while the channel is on require retriggering it afterwards.
            if (!ch2.enabled) {
                ch2_env_sweep_pace = value & 0x7;
                ch2_env_positive = GET_BIT(value, 3);
                ch2_env_vol = (value >> 4) & 0xF;
            }

            reg[reg_id] = value;
            break;
        case REG_NR23:
            // This registers stores the lower 8 bits of channel’s 11-bit “period value”
            ch2.frequency = (ch2.frequency & 0xFF00) | (value & 0xFF);

            reg[reg_id] = value;
            break;
        case REG_NR24:
            /*
            Bit 7   - Trigger (1=Restart channel)  (Write Only)
            Bit 6   - Sound Length enable          (Read/Write) (1=Stop output when length in NR11 expires)
            Bit 2-0 - Period value's higher 3 bits (Write Only)
            */
            ch2.frequency  = (ch2.frequency & 0x00FF) | ( ((u16)(value & 0x7) << 8) & 0xFFFF );
            ch2.len_enabled = GET_BIT(value, 6);
            // trigger the channel
            if (GET_BIT(value, 7)) {
                /*
                * TODO
                Frequency timer is reloaded with period.
                Volume envelope timer is reloaded with period.
                Channel volume is reloaded from NRx2.
                Noise channel's LFSR bits are all set to 1.
                Wave channel's position is set to 0 but sample buffer is NOT refilled.
                Square 1's sweep does several things (see frequency sweep).
                */
                
                ch2.enabled = 1;
                // If length counter is zero, it is set to 64 (256 for wave channel).
                if (ch2.len_timer == 64) ch2.len_timer = 0;
                ch2_env_active = 1;
                // activate parameters that required re-triggering to take effect
                ch2_env_sweep_pace  = reg[REG_NR12] & 0x7;
                ch2_env_positive    = GET_BIT(reg[REG_NR12], 3);
                ch2_env_vol   = (reg[REG_NR12] >> 4) & 0xF;

                ch2_env_sweep_counter = ch2_env_sweep_pace;
                ch2.timer = ((2048 - ch2.frequency) << 2);

                if (!ch2.dac_enabled)
                    ch2.enabled = 0;
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
            ch1.pan_right   = GET_BIT(value, 0);
            ch1.pan_left    = GET_BIT(value, 4);
            
            // TODO add rest of the channels

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

void apu_frame_sequencer_update() {
    frame_sequencer = (frame_sequencer + 1) & 7;
    switch (frame_sequencer) {
        case 0:
            update_length();
            break;
        case 2:
            update_length();            
            update_ch1_sweep();
            break;
        case 4:
            update_length();   
            break;
        case 6:
            update_length();   
            update_ch1_sweep();
            break;
        case 7:
            update_envelope();
            break;
    }
    /*
    if (div_apu_counter % 2 == 0) {
        // sound length event
    }
    if (div_apu_counter == 2 || div_apu_counter == 6) {
        // ch1 freq sweep event
    }
    if (div_apu_counter == 7) {
        // envelope sweep event
    }
    */
}

void ch1_tick() {
    u8 cycles = 4;
    u8 timer_prev = ch1.timer;

    //if (!ch1.enabled) return;

    ch1.timer -= cycles;
    // timer underflow
    if (ch1.timer <= timer_prev) {
        ch1.timer += ((2048 - ch1.frequency) << 2);
        // advance the duty cycle bit
        ch1_sequence = (u8)(ch1_sequence + 1) & 7;
        // fetch new output
        if (ch1.enabled && GET_BIT(sw_duty_cycle[ch1_duty_cycle], ch1_sequence))
            ch1.output = 128 + (u8)(10.0 * ((float)ch1_env_vol / 15.0));
        else
            ch1.output = 128;

        //printf("%d,", ch1.output);
    }
}

void ch2_tick() {
    u8 cycles = 4;
    u8 timer_prev = ch2.timer;

    //if (!ch1.enabled) return;

    ch2.timer -= cycles;
    // timer underflow
    if (ch2.timer <= timer_prev) {
        ch2.timer += ((2048 - ch2.frequency) << 2);
        // advance the duty cycle bit
        ch2_sequence = (u8)(ch2_sequence + 1) & 7;
        // fetch new output
        if (ch2.enabled && GET_BIT(sw_duty_cycle[ch2_duty_cycle], ch2_sequence))
            ch2.output = 128 + (u8)(10.0 * ((float)ch2_env_vol / 15.0));
        else
            ch2.output = 128;

        //printf("%d,", ch1.output);
    }
}

void apu_tick() {
    
    
    ch1_tick();
    ch2_tick();
    
    sample_timer += 4;
    if (sample_timer >= sample_frequency) {
        u8 sample;

        sample_timer -= sample_frequency;
        // gather sample
        sample = (u8)(((float)ch1.output * 0.5) + ((float)ch2.output * 0.5));
        audio_add_sample(sample);
    }
    

}



// PRIVATE  -------------------------------------------------

void update_ch1_sweep() {
    //if (!ch1.enabled || ch1_swe)
}

void update_envelope() {
    if (ch1.enabled && ch1_env_sweep_pace != 0 && ch1_env_sweep_counter < ch1_env_sweep_pace) {
        ch1_env_sweep_counter++;
        if (ch1_env_sweep_counter >= ch1_env_sweep_pace) {
            ch1_env_sweep_counter = 0;

            if (ch1_env_active) {
                if (ch1_env_positive && ch1_env_vol < 15) {
                    ch1_env_vol++;
                    reg[REG_NR12] = (reg[REG_NR12] & 0xF) | (ch1_env_vol << 4);
                }
                else if (!ch1_env_positive && ch1_env_vol > 0) {
                    ch1_env_vol--;
                    reg[REG_NR12] = (reg[REG_NR12] & 0xF) | (ch1_env_vol << 4);
                }
            }

            if (ch1_env_vol == 0 || ch1_env_vol == 15)
                ch1_env_active = 0;
        }
    }

    if (ch2.enabled && ch2_env_sweep_pace != 0 && ch2_env_sweep_counter < ch2_env_sweep_pace) {
        ch2_env_sweep_counter++;
        if (ch2_env_sweep_counter >= ch2_env_sweep_pace) {
            ch2_env_sweep_counter = 0;

            if (ch2_env_active) {
                if (ch2_env_positive && ch2_env_vol < 15) {
                    ch2_env_vol++;
                    reg[REG_NR22] = (reg[REG_NR22] & 0xF) | (ch2_env_vol << 4);
                }
                else if (!ch2_env_positive && ch2_env_vol > 0) {
                    ch2_env_vol--;
                    reg[REG_NR22] = (reg[REG_NR22] & 0xF) | (ch2_env_vol << 4);
                }
            }

            if (ch2_env_vol == 0 || ch2_env_vol == 15)
                ch2_env_active = 0;
        }
    }
}

void update_length() {
    // if enabled - counts up to 64, then cuts of the channel

    //printf("ch1:%d, len_enabled:%d, len:%d\n", ch1.enabled, ch1.len_enabled, ch1.len_timer);
    if (ch1.enabled && ch1.len_enabled) {
        ch1.len_timer++;
        //reg[REG_NR11] = (reg[REG_NR11] & 0xC0) | ch1.len_timer; its write only so no need
        if (ch1.len_timer >= 64) {
            ch1.len_timer = 64;
            //reg[REG_NR11] = (reg[REG_NR11] & 0xC0) | ch1.len_timer; its write only so no need

            ch1.enabled = 0;
        }
    }

    if (ch2.enabled && ch2.len_enabled) {
        ch2.len_timer++;
        if (ch2.len_timer >= 64) {
            ch2.len_timer = 64;
            ch2.enabled = 0;
        }
    }
}

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