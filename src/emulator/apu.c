#include "apu.h"
#include "macros.h"

#include <stdio.h>
#include <string.h>
#include "emu_shared.h"

#include "gb.h"


typedef struct Channel {
    u8  enabled;        // whether a sound channel is enabled or not
    u8  dac_enabled;    // whether a sound channel's DAC is enabled or not
    u8  pan_left;       // sound panning   0/1 flag
    u8  pan_right;
    u8  output;         

    u8  len_enabled;    // 1=Stop output when length in NRX1 expires
    u16 len_timer;      // the higher this value is, the shorter the time before the channel is cut (it counts up to 64 / 255 on wave)
    u16 frequency;      // combination of NRX3 + NRX4(0-2)  
    int timer;          // the timer resets to ((2048 - frequency) * 4) when it reaches 0
} SoundChannel;

typedef struct Envelope {
    u8  enabled;         
    u8  sweep_pace;     // how quickly the volume of the sound changes over time (0=no Sweep)
    u8  sweep_counter;  // internal timer
    u8  positive_flag;  // envelope direction (0=decrease, 1=increase)
    u8  volume;         // initial volume of envelope (0-F) (0=no Sound)
} Envelope;

typedef struct Noise {
    u16 lfsr;
    u8  clock_div;
    u8  lfsr_width_mode;
    u8  clock_shift;
} Noise;

typedef struct FrequencySweep {
    u8  enabled;        // internal flag
    u8  counter;        // internal timer
    u8  pace;           // how quickly the sweep period gets changed over time 0-7 range
    u8  negative_flag;  // sweep increase/decrease 0/1 flag
    u8  shift;          // how big the sweep period change is each iteration
    u16 freq_shadow;    // frequency shadow register
} FrequencySweep;

typedef struct SquareWave {
    u8 duty_cycle;      // defines which waveform is used. the ratio of the time spent low vs. high
    u8 sequence;        // the bit index in the current waveform
} SquareWave;

typedef struct Wave {
    u8 volume;
    u8 position;
} Wave;

u16 sample_frequency = 95;  // how often to gather samples 4194304 Hz / 44100 Hz / 4 t-cycles
u16 sample_timer = 0;       // counts up to sample_frequency
int apu_counter = 0;

u8 apu_enabled;             // copy of register NR52 bit 7 
u8 frame_sequencer;
u8 apu_clock_bit;           // Picks a bit from the internal clock and uses it to increase DIV-APU when detects a falling edge (1 to 0)

u8 vol_l, vol_r;            // master volume   0-7 range
u8 vin_pan_l, vin_pan_r;    // VIN panning     0/1 flag


const u8 sw_duty_cycle[4] = {   // square channel duty cycle waveforms
    0x1,    // 12.5%    00000001
    0x81,   // 25.0%    10000001
    0x87,   // 50.0%    10000111
    0x7E    // 75.0%    01111110
};

const u8 nw_divisors[8] = { // noise channel clock divider's divisor
    8, 16, 32, 48, 64, 80, 96, 112
};


SoundChannel    ch1;
Envelope        ch1_env;
FrequencySweep  ch1_sweep;
SquareWave      ch1_sqw;

SoundChannel    ch2;
Envelope        ch2_env;
SquareWave      ch2_sqw;

SoundChannel    ch3;
Wave            ch3_wave;

SoundChannel    ch4;
Envelope        ch4_env;
Noise           ch4_noise;


// Forward declaration
void    turn_off();
void    update_length();
void    update_envelope();
void    update_ch1_sweep();
u16     calculate_ch1_sweep_frequency();


// PUBLIC   -------------------------------------------------

void apu_init() {

}

void apu_powerup()
{
    apu_enabled     = 1;
    frame_sequencer = 0;
    apu_clock_bit   = 12;

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

    apu_write_register(REG_NR50, reg[REG_NR50]);
    apu_write_register(REG_NR51, reg[REG_NR51]);
    apu_write_register(REG_NR52, reg[REG_NR52]);

    ch1.dac_enabled = 1;
    ch1.enabled     = 1;
    ch1_env.sweep_counter = 0;
    ch1_env.enabled  = 1;
    apu_write_register(REG_NR10, reg[REG_NR10]);
    apu_write_register(REG_NR11, reg[REG_NR11]);
    apu_write_register(REG_NR12, reg[REG_NR12]);
    apu_write_register(REG_NR13, reg[REG_NR13]);
    apu_write_register(REG_NR14, reg[REG_NR14]);

    ch2.dac_enabled = 1;
    ch2.enabled     = 1;
    ch2_env.sweep_counter = 0;
    ch2_env.enabled  = 1;
    apu_write_register(REG_NR21, reg[REG_NR21]);
    apu_write_register(REG_NR22, reg[REG_NR22]);
    apu_write_register(REG_NR23, reg[REG_NR23]);
    apu_write_register(REG_NR24, reg[REG_NR24]);
    
    ch3.dac_enabled = 1;
    ch3.enabled     = 1;
    ch3_wave.position = 1;
    apu_write_register(REG_NR30, reg[REG_NR30]);
    apu_write_register(REG_NR31, reg[REG_NR31]);
    apu_write_register(REG_NR32, reg[REG_NR32]);
    apu_write_register(REG_NR33, reg[REG_NR33]);
    apu_write_register(REG_NR34, reg[REG_NR34]);

    ch4.dac_enabled = 1;
    ch4.enabled     = 1;
    ch4_env.sweep_counter = 0;
    ch4_env.enabled  = 1;
    apu_write_register(REG_NR41, reg[REG_NR41]);
    apu_write_register(REG_NR42, reg[REG_NR42]);
    apu_write_register(REG_NR43, reg[REG_NR43]);
    apu_write_register(REG_NR44, reg[REG_NR44]);
}

u8 apu_read_register(u8 reg_id) {
    switch (reg_id) {
        // Pulse/Square Channel 1
        case REG_NR11:
            return (reg[reg_id] | 0x3F); // bits 0~5 are write only (masked with 0b 0011 1111)
        case REG_NR13:
            return 0xFF; // write only
        case REG_NR14:
            return (reg[reg_id] | 0xBF); // only bit 6 is readable (masked with 0b 1011 1111)
        
        // Pulse/Square Channel 2
        case REG_NR21:
            return (reg[reg_id] | 0x3F); // bits 0~5 are write only (masked with 0b 0011 1111)
        case REG_NR23:
            return 0xFF; // write only
        case REG_NR24:
            return (reg[reg_id] | 0xBF); // only bit 6 is readable (masked with 0b 1011 1111)

        // Wave Channel 3
        case REG_NR33:
            return 0xFF; // write only
        case REG_NR34:
            return (reg[reg_id] | 0xBF); // only bit 6 is readable (masked with 0b 1011 1111)

        // Wave Channel 4
        case REG_NR44:
            return (reg[reg_id] | 0xBF); // only bit 6 is readable (masked with 0b 1011 1111)

        default:
            return reg[reg_id];
    }
}
int apu_write_register(u8 reg_id, u8 value) {
    // Turning the APU off makes its registers read-only until turned back on, except NR52 and Wave RAM
    if (!apu_enabled && reg_id != REG_NR52 && reg_id < REG_WAVERAM) return -1;

    switch (reg_id) {
        // Square 1 & Sweep Channel
        case REG_NR10:
            /*
            Bit 6-4 - Sweep pace
            Bit 3   - Sweep increase/decrease (0: Addition (period increases) 1: Subtraction (period decreases))
            Bit 2-0 - Sweep slope control (n: 0-7)
            */
            // The pace is only reloaded after the following sweep iteration, or when (re)triggering the channel. 
            // However, if bits 4–6 are all set to 0, then iterations are instantly disabled, 
            // and the pace will be reloaded immediately if it’s set to something else.

            if (ch1_sweep.pace == 0) {
                ch1_sweep.enabled   = 0;
                ch1_sweep.pace      = (value >> 4) & 0x7;
            }
            ch1_sweep.negative_flag = GET_BIT(value, 3);
            ch1_sweep.shift         = value & 0x7;

            reg[reg_id] = value | 0x80;

            break;
        case REG_NR11:
            /*
            Bit 7-6 - Wave duty            (Read/Write)
            Bit 5-0 - Initial length timer (Write Only)
            */
            ch1_sqw.duty_cycle  = (value >> 6) & 0x3;
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
                ch1_env.sweep_pace      = value & 0x7;
                ch1_env.positive_flag   = GET_BIT(value, 3);
                ch1_env.volume          = (value >> 4) & 0xF;
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
            ch1.frequency   = (ch1.frequency & 0x00FF) | ( ((u16)(value & 0x7) << 8) & 0xFFFF );
            ch1.len_enabled = GET_BIT(value, 6);
            // trigger the channel
            if (GET_BIT(value, 7)) {
                ch1.enabled = 1;
                // frequency is copied to the shadow register.
                ch1_sweep.freq_shadow = ch1.frequency;
                // the internal enabled flag is set if either the sweep period or shift are non-zero, cleared otherwise.
                ch1_sweep.enabled = (ch1_sweep.pace || ch1_sweep.shift);
                // if the sweep shift is non-zero, frequency calculation and the overflow check are performed immediately.
                if (ch1_sweep.shift != 0) 
                    ch1.frequency = calculate_ch1_sweep_frequency();
                // if length counter is zero, it is set to 64.
                if (ch1.len_timer == 64) ch1.len_timer = 0;
                ch1_env.enabled = 1;
                // activate parameters that required re-triggering to take effect
                ch1_env.sweep_pace      = reg[REG_NR12] & 0x7;
                ch1_env.positive_flag   = GET_BIT(reg[REG_NR12], 3);
                ch1_env.volume          = (reg[REG_NR12] >> 4) & 0xF;
                // the sweep timer is reloaded.
                ch1_env.sweep_counter   = (ch1_env.sweep_pace == 0) ? 8 : ch1_env.sweep_pace;
                ch1_sweep.counter       = (ch1_sweep.pace == 0) ? 8 : ch1_sweep.pace;

                ch1.timer = ((2048 - ch1.frequency) << 2);

                if (!ch1.dac_enabled)
                    ch1.enabled = 0;
            }
            reg[reg_id] = value;
            break;

        // Square 2 Channel
        case REG_NR21:
            /*
            Bit 7-6 - Wave duty            (Read/Write)
            Bit 5-0 - Initial length timer (Write Only)
            */
            ch2_sqw.duty_cycle  = (value >> 6) & 0x3;
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
                ch2_env.sweep_pace      = value & 0x7;
                ch2_env.positive_flag   = GET_BIT(value, 3);
                ch2_env.volume          = (value >> 4) & 0xF;
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
            ch2.frequency   = ( ((u16)(value & 0x7) << 8) & 0xFF00 ) | (ch2.frequency & 0x00FF);
            ch2.len_enabled = GET_BIT(value, 6);
            // trigger the channel
            if (GET_BIT(value, 7)) {
                ch2.enabled = 1;

                // if length counter is zero, it is set to 64 (256 for wave channel).
                if (ch2.len_timer == 64) ch2.len_timer = 0;
                ch2_env.enabled = 1;
                // activate parameters that required re-triggering to take effect
                ch2_env.sweep_pace      = reg[REG_NR22] & 0x7;
                ch2_env.positive_flag   = GET_BIT(reg[REG_NR22], 3);
                ch2_env.volume          = (reg[REG_NR22] >> 4) & 0xF;
                // the sweep timer is reloaded.
                ch2_env.sweep_counter   = (ch2_env.sweep_pace == 0) ? 8 : ch2_env.sweep_pace;

                ch2.timer = ((2048 - ch2.frequency) << 2);

                if (!ch2.dac_enabled)
                    ch2.enabled = 0;
            }
            reg[reg_id] = value;
            break;

        // Wave Channel
        case REG_NR30:
            ch3.dac_enabled = GET_BIT(value, 7);
            if (!ch3.dac_enabled) ch3.enabled = 0;

            reg[reg_id] = value | 0x7F;
            break;
        case REG_NR31:
            ch3.len_timer = value;

            reg[reg_id] = value;
            break;
        case REG_NR32:
            /*
            %00	Mute (No sound)
            %01	100% volume (use samples read from Wave RAM as-is)
            %10	50% volume (shift samples read from Wave RAM right once)
            %11	25% volume (shift samples read from Wave RAM right twice)
            */
            ch3_wave.volume = (value >> 5) & 3;
            reg[reg_id] = value | 0x9F;
            break;
        case REG_NR33:
            // This registers stores the lower 8 bits of channel’s 11-bit “period value”
            // changes only take effect after the following time wave RAM is read.
            // ch3.frequency = (((reg[REG_NR34] >> 5) << 8) & 0xFF00) | (reg[REG_NR33] & 0xFF);
            reg[reg_id] = value;
            break;
        case REG_NR34:
            /*
            Bit 7   - Trigger (1=Restart channel)  (Write Only)
            Bit 6   - Sound Length enable          (Read/Write) (1=Stop output when length in NR11 expires)
            Bit 2-0 - Period value's higher 3 bits (Write Only)
            */
            ch3.len_enabled = GET_BIT(value, 6);
            // trigger the channel
            if (GET_BIT(value, 7)) {
                /*
                TODO
                Frequency timer is reloaded with period.
                Channel volume is reloaded from NRx2.
                Wave channel's position is set to 0 but sample buffer is NOT refilled.
                */
                ch3.enabled = 1;

                ch3.timer = ((2048 - ch3.frequency) << 1);
                if (ch3.len_timer == 256) ch3.len_timer = 0;
                ch3_wave.position = 0;

                if (!ch3.dac_enabled)
                    ch3.enabled = 0;
            }
            reg[reg_id] = value;
            break;

        // Noise channel
        case REG_NR41:
            ch4.len_timer = value & 0x3F;

            reg[reg_id] = value | 0xC0;
            break;

        case REG_NR42:
            /*
            Bit 7-4 - Initial volume of envelope (0-F) (0=No Sound)
            Bit 3   - Envelope direction (0=Decrease, 1=Increase)
            Bit 2-0 - Sweep pace (0=No Sweep)
            */

            // Setting bits 3-7 of this register all to 0 turns the DAC off, which also turns the channel off
            ch4.dac_enabled = ((value & 0xF8) != 0);
            if (!ch4.dac_enabled) ch4.enabled = 0; // channel can only turn off by the DAC, it can't turn it on
            
            // Writes to this register while the channel is on require retriggering it afterwards.
            if (!ch4.enabled) {
                ch4_env.sweep_pace      = value & 0x7;
                ch4_env.positive_flag   = GET_BIT(value, 3);
                ch4_env.volume          = (value >> 4) & 0xF;
            }

            reg[reg_id] = value;
            break;

        case REG_NR43:
            /*
            Bit 7-4 - Clock shift (s)
            Bit 3   - LFSR width (0=15 bits, 1=7 bits)
            Bit 2-0 - Clock divider (r)
            */
            ch4_noise.clock_div         = value & 7;
            ch4_noise.lfsr_width_mode   = GET_BIT(value, 3);
            ch4_noise.clock_shift       = (value >> 4) & 0xF;

            reg[reg_id] = value;
            break;

        case REG_NR44:
            /*
            Bit 7   - Trigger (1=Restart channel)  (Write Only)
            Bit 6   - Sound Length enable          (Read/Write) (1=Stop output when length in NR11 expires)
            */
            ch4.len_enabled = GET_BIT(value, 6);
            // trigger the channel
            if (GET_BIT(value, 7)) {
                ch4.enabled = 1;

                if (ch4.len_timer == 64) ch4.len_timer = 0;
                ch4_env.enabled = 1;
                // activate parameters that required re-triggering to take effect
                ch4_env.sweep_pace      = reg[REG_NR42] & 0x7;
                ch4_env.positive_flag   = GET_BIT(reg[REG_NR42], 3);
                ch4_env.volume          = (reg[REG_NR42] >> 4) & 0xF;
                // the sweep timer is reloaded.
                ch4_env.sweep_counter   = (ch4_env.sweep_pace == 0) ? 8 : ch4_env.sweep_pace;

                ch4.timer = nw_divisors[ch4_noise.clock_div] << ch4_noise.clock_shift;

                // noise channel's LFSR bits are all set to 1 (15 bit)
                ch4_noise.lfsr = 0x7FFF;

                if (!ch4.dac_enabled)
                    ch4.enabled = 0;
            }
            reg[reg_id] = value | 0xC0;
            break;

        // Global sound registers
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
                //apu_powerup();
            }
            break;
        // Wave RAM
        case REG_WAVERAM:
        case 0x31: case 0x32: case 0x33: case 0x34: case 0x35: case 0x36:
        case 0x37: case 0x38: case 0x39: case 0x3A: case 0x3B: case 0x3C:
        case 0x3D: case 0x3E: case 0x3F:
            reg[reg_id] = value;
            break;

        // Unused
        default:
            reg[reg_id] = 0xFF;
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
}

void ch1_tick() {
    u8 clock = M_CYCLE;
    u8 timer_prev = ch1.timer;

    //if (!ch1.enabled) return;

    ch1.timer -= clock;
    // timer underflow
    if (ch1.timer <= timer_prev) {
        ch1.timer += ((2048 - ch1.frequency) << 2);
        // advance the duty cycle bit
        ch1_sqw.sequence = (u8)(ch1_sqw.sequence + 1) & 7;
        // fetch new output
        if (ch1.enabled && GET_BIT(sw_duty_cycle[ch1_sqw.duty_cycle], ch1_sqw.sequence))
            ch1.output = ch1_env.volume;
        else
            ch1.output = 0;

        //printf("%d,", ch1.output);
    }
}
void ch2_tick() {
    u8 clock = M_CYCLE;
    u8 timer_prev = ch2.timer;

    ch2.timer -= clock;
    // timer underflow
    if (ch2.timer <= timer_prev) {
        ch2.timer += ((2048 - ch2.frequency) << 2);
        // advance the duty cycle bit
        ch2_sqw.sequence = (u8)(ch2_sqw.sequence + 1) & 7;
        // fetch new output
        if (ch2.enabled && GET_BIT(sw_duty_cycle[ch2_sqw.duty_cycle], ch2_sqw.sequence))
            ch2.output = ch2_env.volume;
        else
            ch2.output = 0;
    }
}
void ch3_tick() {
    u8 clock = M_CYCLE;
    u8 timer_prev = ch3.timer;

    ch3.timer -= clock;
    // timer underflow
    if (ch3.timer <= timer_prev) {
        ch3.timer += ((2048 - ch3.frequency) << 1);

        // advance the wave ram position
        ch3_wave.position = (u8)(ch3_wave.position + 1) & 0x1F;

        // fetch new output
        if (ch3.enabled) {
            u8 nibble_mask = ((ch3_wave.position & 1) == 0) ? 0xF0 : 0x0F;
            u8 wave_val = reg[REG_WAVERAM + (ch3_wave.position >> 1)] & nibble_mask;
            u8 volume = 0;

            if ((ch3_wave.position & 1) == 0)
                wave_val >>= 4;

            wave_val &= 0xF;

            switch (ch3_wave.volume) {
                /*
                %00	Mute (No sound)
                %01	100% volume (use samples read from Wave RAM as-is)
                %10	50% volume (shift samples read from Wave RAM right once)
                %11	25% volume (shift samples read from Wave RAM right twice)
                */
                case 0: volume = 0;             break;
                case 1: volume = wave_val;      break;
                case 2: volume = wave_val >> 1; break;
                case 3: volume = wave_val >> 2; break;
            }
            ch3.output = volume;
        }
        else
            ch3.output = 0;

        // reload frequency
        ch3.frequency = ((((reg[REG_NR34] & 7) & 0xFF) << 8) & 0xFF00) | (reg[REG_NR33] & 0xFF);
    }
}
void ch4_tick() {
    u8 clock = M_CYCLE;
    u8 timer_prev = ch4.timer;

    ch4.timer -= clock;
    // timer underflow
    if (ch4.timer <= timer_prev) {
        u8 xor_val;
        // The noise channel's frequency timer period is set by a base divisor shifted left some number of bits. 
        ch4.timer = (nw_divisors[ch4_noise.clock_div] << ch4_noise.clock_shift);
        // generate a pseudo-random sequence
        xor_val = GET_BIT(ch4_noise.lfsr, 0) ^ GET_BIT(ch4_noise.lfsr, 1);
        ch4_noise.lfsr >>= 1;
        ch4_noise.lfsr = (ch4_noise.lfsr & 0x3FFF) | (xor_val << 14);
        // If width mode is 1, the XOR result is ALSO put into bit 6 AFTER the shift, resulting in a 7-bit LFSR
        if (ch4_noise.lfsr_width_mode == 1) {
            ch4_noise.lfsr = (ch4_noise.lfsr & 0xFFBF) | (xor_val << 6);
        }
        // fetch new output
        if (ch4.enabled && !GET_BIT(ch4_noise.lfsr, 0))
            ch4.output = ch4_env.volume;
        else
            ch4.output = 0;
    }
}


void apu_tick() {
    if (!apu_enabled) return;

    ch1_tick();
    ch2_tick();
    ch3_tick();
    ch4_tick();

    sample_timer += M_CYCLE;
    if (sample_timer >= sample_frequency) {
        // Generates an audio sample and outputs it to SDL
        float output;

        sample_timer -= sample_frequency;

        // when speeding up the emulator, only collect every Nth sample
        if (apu_counter++ % gb_frameskip != 0) return;
        
        // TODO add stereo (mono only)
        output = (ch1.output + ch2.output + ch3.output + ch4.output) * 2.0f;
        output = output * ((float)(vol_l + vol_r) / 14.0f);

        gb_output_audio_sample((u8)output);
    }
    

}



// PRIVATE  -------------------------------------------------

void update_ch1_sweep() {
    if (ch1.enabled && ch1_sweep.counter > 0) {
        ch1_sweep.counter--;
        if (ch1_sweep.counter == 0) {
            ch1_sweep.counter = ch1_sweep.pace;
            
            // The volume envelope and sweep timers treat a period of 0 as 8.
            if (ch1_sweep.pace == 0)
                ch1_sweep.counter = 8; 

            if (ch1_sweep.enabled) {
                /* If the new frequency is 2047 or less and the sweep shift is not zero, 
                this new frequency is written back to the shadow frequency and square 1's frequency in NR13 and NR14, 
                then frequency calculation and overflow check are run AGAIN immediately using this new value, 
                but this second new frequency is not written back. */
                u16 freq_new = calculate_ch1_sweep_frequency();
                if (freq_new <= 2047 && ch1_sweep.shift != 0) {
                    ch1.frequency = freq_new;
                    reg[REG_NR13] = freq_new & 0xFF;
                    reg[REG_NR14] = (reg[REG_NR14] & 0x1F) | ((freq_new >> 8) & 0xFF);

                    ch1_sweep.freq_shadow = freq_new;
                    calculate_ch1_sweep_frequency();
                }
            }
        }
    }
    // reload the sweep pace after this interation
    ch1_sweep.pace = (reg[REG_NR10] >> 4) & 0x7;
}

u16 calculate_ch1_sweep_frequency() {
    /* Frequency calculation consists of taking the value in the frequency shadow register, 
    shifting it right by sweep shift, optionally negating the value, 
    and summing this with the frequency shadow register to produce a new frequency. 
    What is done with this new frequency depends on the context.
    The overflow check simply calculates the new frequency and if this is greater than 2047, square 1 is disabled. */
    u16 freq_new;
    u16 shift_val = ch1_sweep.freq_shadow >> ch1_sweep.shift;
    
    freq_new = ch1_sweep.negative_flag ? ch1_sweep.freq_shadow - shift_val : ch1_sweep.freq_shadow + shift_val;

    if (freq_new > 2047)
        ch1.enabled = 0;

    return freq_new;
}

void update_envelope() {
    if (ch1.enabled && ch1_env.sweep_counter > 0) {
        ch1_env.sweep_counter--;
        if (ch1_env.sweep_counter == 0) {
            ch1_env.sweep_counter = ch1_env.sweep_pace;
            
            // The volume envelope and sweep timers treat a period of 0 as 8.
            if (ch1_env.sweep_pace == 0)
                ch1_env.sweep_counter = 8; 

            if (ch1_env.enabled && ch1_env.sweep_pace > 0) {
                if (ch1_env.positive_flag && ch1_env.volume < 15) {
                    ch1_env.volume++;
                    reg[REG_NR12] = (reg[REG_NR12] & 0xF) | (ch1_env.volume << 4);
                }
                else if (!ch1_env.positive_flag && ch1_env.volume > 0) {
                    ch1_env.volume--;
                    reg[REG_NR12] = (reg[REG_NR12] & 0xF) | (ch1_env.volume << 4);
                }
            }

            if (ch1_env.volume == 0 || ch1_env.volume == 15)
                ch1_env.enabled = 0;
        }
    }

    if (ch2.enabled && ch2_env.sweep_counter > 0) {
        ch2_env.sweep_counter--;
        if (ch2_env.sweep_counter == 0) {
            ch2_env.sweep_counter = ch2_env.sweep_pace;
            
            // The volume envelope and sweep timers treat a period of 0 as 8.
            if (ch2_env.sweep_pace == 0)
                ch2_env.sweep_counter = 8; 

            if (ch2_env.enabled && ch2_env.sweep_pace > 0) {
                if (ch2_env.positive_flag && ch2_env.volume < 15) {
                    ch2_env.volume++;
                    reg[REG_NR22] = (reg[REG_NR22] & 0xF) | (ch2_env.volume << 4);
                }
                else if (!ch2_env.positive_flag && ch2_env.volume > 0) {
                    ch2_env.volume--;
                    reg[REG_NR22] = (reg[REG_NR22] & 0xF) | (ch2_env.volume << 4);
                }
            }

            if (ch2_env.volume == 0 || ch2_env.volume == 15)
                ch2_env.enabled = 0;
        }
    }

    if (ch4.enabled && ch4_env.sweep_counter > 0) {
        ch4_env.sweep_counter--;
        if (ch4_env.sweep_counter == 0) {
            ch4_env.sweep_counter = ch4_env.sweep_pace;
            
            // The volume envelope and sweep timers treat a period of 0 as 8.
            if (ch4_env.sweep_pace == 0)
                ch4_env.sweep_counter = 8; 

            if (ch4_env.enabled && ch4_env.sweep_pace > 0) {
                if (ch4_env.positive_flag && ch4_env.volume < 15) {
                    ch4_env.volume++;
                    reg[REG_NR42] = (reg[REG_NR42] & 0xF) | (ch4_env.volume << 4);
                }
                else if (!ch4_env.positive_flag && ch4_env.volume > 0) {
                    ch4_env.volume--;
                    reg[REG_NR42] = (reg[REG_NR42] & 0xF) | (ch4_env.volume << 4);
                }
                //printf("%d,", ch4_env.volume);
            }

            if (ch4_env.volume == 0 || ch4_env.volume == 15)
                ch4_env.enabled = 0;
        }
    }
}

void update_length() {
    // if enabled - counts up to 64/256, then cuts off the channel

    //printf("ch4:%d, len_enabled:%d, len:%d\n", ch4.enabled, ch4.len_enabled, ch4.len_timer);
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

    if (ch3.enabled && ch3.len_enabled) {
        ch3.len_timer++;
        if (ch3.len_timer >= 256) {
            ch3.len_timer = 256;
            ch3.enabled = 0;
        }
    }

    if (ch4.enabled && ch4.len_enabled) {
        ch4.len_timer++;
        if (ch4.len_timer >= 64) {
            ch4.len_timer = 64;
            ch4.enabled = 0;
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
    reg[REG_NR52] = 0;

    for (int i = 0; i < 16; i++) {
        reg[REG_WAVERAM + i] = 0;
    }

    sample_timer = 0; 
    apu_counter = 0;

    frame_sequencer = 0;

    vol_l = 0; vol_r = 0;
    vin_pan_l = 0; vin_pan_r = 0;

    // Clear all channel variables
    memset(&ch1,        0, sizeof(SoundChannel));
    memset(&ch1_env,    0, sizeof(Envelope));
    memset(&ch1_sqw,    0, sizeof(SquareWave));
    memset(&ch1_sweep,  0, sizeof(FrequencySweep));

    memset(&ch2,        0, sizeof(SoundChannel));
    memset(&ch2_env,    0, sizeof(Envelope));
    memset(&ch2_sqw,    0, sizeof(SquareWave));

    memset(&ch3,        0, sizeof(SoundChannel));
    memset(&ch3_wave,   0, sizeof(Wave));

    memset(&ch4,        0, sizeof(SoundChannel));
    memset(&ch4_env,    0, sizeof(Envelope));
    memset(&ch4_noise,  0, sizeof(Noise));
}