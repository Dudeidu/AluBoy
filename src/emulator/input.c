#include "input.h"
#include "macros.h"

#include <stdio.h>
#include <string.h>
#include "emu_shared.h"

// Last input retrieved
u8 inputs[8];
u8 inputs_direction = 0x0F;
u8 inputs_action = 0x0F;
u8 inputs_update_line;

// PUBLIC   -------------------------------------------------

void input_update(u8* in)
{
    // Update inputs from SDL
    memcpy(inputs, in, 8);
}

void input_tick()
{
    u8 p1 = reg[REG_P1];

    u8 select_direction = !GET_BIT(p1, 4);
    u8 select_action    = !GET_BIT(p1, 5);

    if (!select_direction && !select_action) {
        p1 = 0xFF; // Set all bits to 1 if neither condition is met
    }
    else {
        p1 = 0xF0; // Start with upper nibble set
        //if (inputs_action != 15) printf("%d", inputs_action);
        //if (inputs_direction != 15) printf("%d", inputs_direction);
        if (select_direction && !select_action) {
            RESET_BIT(p1, 4);
            p1 |= (inputs_direction);
        }
        else if (select_action && !select_direction) {
            RESET_BIT(p1, 5);
            p1 |= (inputs_action);
        }
        else {
            RESET_BIT(p1, 4);
            RESET_BIT(p1, 5);
            p1 |= (inputs_direction & inputs_action);
        }
    }
    // The Joypad interrupt is requested when any of P1 bits 0-3 change from High to Low
    // Indicating that a button was pressed
    if (reg[REG_P1] != p1) {
        // Check for changes from high to low for bits 0-3
        u8 change_mask = (reg[REG_P1] & 0x0F) & ~(p1 & 0x0F);
        if (change_mask > 0) {
            // Joypad interrupt
            SET_BIT(reg[REG_IF], INT_BIT_JOYPAD);
        }

        reg[REG_P1] = p1;
    }
}

void input_joypad_update()
{
    if (input_updated || reg[REG_LY] != inputs_update_line) return;

    // Emulate D-pad behavior where you can't physically press both opposing directions at the same time
    if (inputs[0] && inputs[1]) inputs[0] = 0;
    if (inputs[2] && inputs[3]) inputs[2] = 0;

    inputs_direction = ((!inputs[3] << 3) | (!inputs[2] << 2) | (!inputs[1] << 1) | (!inputs[0] << 0));
    inputs_action = ((!inputs[7] << 3) | (!inputs[6] << 2) | (!inputs[5] << 1) | (!inputs[4] << 0));

    inputs_update_line = (inputs_update_line + 5) % 20;
    input_updated = 1;

    //if (inputs_direction != dir_prev) printf("%2X", inputs_direction);
    //if (inputs_action != action_prev) printf("%2X", inputs_action);
}

// PRIVATE  -------------------------------------------------
