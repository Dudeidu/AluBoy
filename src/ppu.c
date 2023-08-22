#include "ppu.h"
#include "macros.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "emu_shared.h"

#define PIXELS_PER_BYTE 1
#define BITS_PER_PIXEL  8

u8* sprite_buffer      = NULL;
u8* background_buffer  = NULL;
u8* window_buffer      = NULL;

u8 redraw_flag;

u16 scanline_counter; // every >SCANLINE_DOTS, update LY
u16 lcd_mode_next;    // determines when (scanline_counter) to switch lcd_mode
u16 objects[40];      // array of object indexes that intersect with of the current scanline
u8  object_count;     // how many objects to read from the array


// Drawing optimization
int vblank_counter  = 0;
u8  frameskip       = 1; // only draw the screen when vblank_counter % frameskip == 0
u16 tm_addr_prev    = 0;


u8  pal_bgp[4]      = { 0, 1, 2, 3 };
u8  pal_obp0[4]     = { 0, 1, 2, 3 };
u8  pal_obp1[4]     = { 0, 1, 2, 3 };

// Shared
u8 lcd_mode = LCD_MODE_VBLANK;
u8 lcd_enabled      = 1;

// FORWARD DECLARE
void draw_scanline(u8 y);
void draw_tiles(u8 y);
void draw_sprites(u8 y);
u8 search_oam(u8 y);

// PUBLIC --------------------------------------------------

// Initialize
int ppu_init()
{
    // Allocate memory for the buffer and initialize with 0
    int buffer_size = (SCREEN_WIDTH * SCREEN_HEIGHT) / PIXELS_PER_BYTE;
    sprite_buffer = (u8*)calloc(buffer_size, sizeof(u8));
    if (sprite_buffer == NULL)
    {
        fprintf(stderr, "Failed to allocate memory for the pixel buffer!\n");
        return -1;
    }

    background_buffer = (u8*)calloc(buffer_size, sizeof(u8));
    if (background_buffer == NULL)
    {
        fprintf(stderr, "Failed to allocate memory for the background buffer!\n");
        return -1;
    }

    window_buffer = (u8*)calloc(buffer_size, sizeof(u8));
    if (window_buffer == NULL)
    {
        fprintf(stderr, "Failed to allocate memory for the window buffer!\n");
        return -1;
    }

    redraw_flag = 1;
    return 0;
}

// Used for passing the pixel buffer to GL
u8* ppu_get_pixel_buffer()
{
    return background_buffer;
}

// assigns gray shades to the color indexes
void ppu_update_palette(u8 reg, u8 value) {
    u8* palette;
    switch (reg) {
        case REG_BGP:   palette = pal_bgp; break;
        case REG_OBP0:  palette = pal_obp0; break;
        case REG_OBP1:  palette = pal_obp1; break;
        default: return;
    }
    for (int i = 0; i < 4; i++) {
        palette[i] = (value >> (i * 2)) & 0x3;
    }
}

void ppu_clear_screen() {
    int buffer_size = (SCREEN_WIDTH * SCREEN_HEIGHT) / PIXELS_PER_BYTE;

    reg[REG_LY] = 0;
    vblank_counter = 0;
    scanline_counter = 0;
    RESET_BIT(reg[REG_IF], INT_BIT_VBLANK);

    memset(background_buffer, MEM_ROM_0, buffer_size);
}

// Whether the screen needs to be redrawn
u8 ppu_get_redraw_flag() {
    return redraw_flag && (vblank_counter % frameskip == 0);
}

void ppu_set_redraw_flag(u8 val) {
    redraw_flag = val;
}

void ppu_tick(u8 cycles)
{
    u8 clock = cycles;
    u8 new_frame = 0;

    //printf("%d,", reg[REG_STAT]);
    scanline_counter += clock;
    if (scanline_counter < lcd_mode_next) return;
    switch (lcd_mode) {
        case LCD_MODE_OAM: // Searching OAM for OBJs whose Y coordinate overlap this line
            reg[REG_STAT] = (reg[REG_STAT] & ~0x3) | LCD_MODE_VRAM;
            lcd_mode = LCD_MODE_VRAM;
            lcd_mode_next += 172;
            break;
        case LCD_MODE_VRAM: // Reading OAM and VRAM to generate the picture
            // Check for coincidence interrupt (if LYC=LY and interrupt is enabled)
            if (reg[REG_LY] == reg[REG_LYC]) SET_BIT(reg[REG_STAT], 2);
            else RESET_BIT(reg[REG_STAT], 2);

            if (GET_BIT(reg[REG_STAT], 2) && GET_BIT(reg[REG_STAT], STAT_INT_LYC))
            {
                // request interrupt
                SET_BIT(reg[REG_IF], INT_BIT_STAT);
            }

            // request hblank interrupt if enabled
            if (GET_BIT(reg[REG_STAT], STAT_INT_HBLANK)) {
                SET_BIT(reg[REG_IF], INT_BIT_STAT);
            }

            reg[REG_STAT] = (reg[REG_STAT] & ~0x3) | LCD_MODE_HBLANK;
            lcd_mode = LCD_MODE_HBLANK;
            lcd_mode_next = SCANLINE_DOTS;
            break;
        case LCD_MODE_HBLANK:
            scanline_counter -= SCANLINE_DOTS;

            // Draw scanline //////////////////////////////
            object_count = search_oam(reg[REG_LY]);
            if (lcd_enabled && vblank_counter % frameskip == 0) {
                draw_scanline(reg[REG_LY]);
            }

            // Move to a new scanline
            reg[REG_LY] ++;

            // Update inputs at a different LY each frame to avoid detection
            if (!input_updated) {
                input_joypad_update();
            }

            if (reg[REG_LY] == SCREEN_HEIGHT) {
                // Vblank period start
                vblank_counter++;

                // Vblank interrupt request
                SET_BIT(reg[REG_IF], INT_BIT_VBLANK);

                // request interrupt if enabled
                if (GET_BIT(reg[REG_STAT], STAT_INT_VBLANK)) {
                    SET_BIT(reg[REG_IF], INT_BIT_STAT);
                }

                lcd_mode = LCD_MODE_VBLANK;
                reg[REG_STAT] = (reg[REG_STAT] & ~0x3) | LCD_MODE_VBLANK;
                lcd_mode_next = SCANLINE_DOTS;
            }
            else {
                // request interrupt if enabled
                if (GET_BIT(reg[REG_STAT], STAT_INT_OAM)) {
                    SET_BIT(reg[REG_IF], INT_BIT_STAT);
                }
                // change lcd mode to oam search (0~80)
                lcd_mode = LCD_MODE_OAM;
                reg[REG_STAT] = (reg[REG_STAT] & ~0x3) | LCD_MODE_OAM;
                lcd_mode_next = 80;
            }
            break;
        case LCD_MODE_VBLANK:
            scanline_counter -= SCANLINE_DOTS;

            // Move to a new scanline
            reg[REG_LY] ++;
            
            // Reached last line
            if (reg[REG_LY] >= 0x9A) {
                reg[REG_LY] = 0;
                // reset input fetching
                input_updated = 0;

                if (!input_updated) {
                    input_joypad_update();
                }
                // request interrupt if enabled
                if (GET_BIT(reg[REG_STAT], STAT_INT_OAM)) {
                    SET_BIT(reg[REG_IF], INT_BIT_STAT);
                }
                // change lcd mode to oam search (0~80)
                lcd_mode = LCD_MODE_OAM;
                reg[REG_STAT] = (reg[REG_STAT] & ~0x3) | LCD_MODE_OAM;
                lcd_mode_next = 80;
            }
            break;
    }
}

void ppu_cleanup()
{
    if (sprite_buffer)      free(sprite_buffer);
    if (background_buffer)  free(background_buffer);
    if (window_buffer)      free(window_buffer);
}

// PRIVATE --------------------------------------------------

// Returns whether or not the pixel was modified
u8 set_pixel(u16 pos, u8 color_index)
{
    u8 color_index_prev = background_buffer[pos];
    // Set the pixel value in the pixel buffer
    background_buffer[pos] = color_index;

    return (color_index_prev != color_index);
}

// Searching OAM for OBJs whose Y coordinate overlap this scanline, and return the amount.
u8 search_oam(u8 y) {
    u8 count = 0;
    u8 obj_height = GET_BIT(reg[REG_LCDC], LCDC_OBJ_SZ) ? 16 : 8; // 8x16 sprites

    // iterate through all objects in OAM, count if pixel intersects with our scanline
    for (s8 obj = 0; obj < 40; obj++) {
        u8 index = obj << 2; // obj * 4
        u8 ypos = oam[index];
        short ty; // the sprite line to draw

        // check if outside screen
        if ((ypos + obj_height) <= 16 || ypos >= 160) {
            continue;
        }
        // check if intersecting
        ty = (y - (ypos - 16));
        if (ty < 0 || ty >= obj_height) continue;

        // add to array of intersecting object indexes
        objects[count++] = index;
        // count towards scanline limit (PPU only checks the Y coordinate to select objects)
        if (count == 10) return count;
    }
    return count;
}

void draw_scanline(u8 y) {
    if (GET_BIT(reg[REG_LCDC], LCDC_BGW_ENABLE)) {
        draw_tiles(y);
    }
    else {
        // draw white color
        //memset(&background_buffer[y * SCREEN_WIDTH], 0, SCREEN_WIDTH);
        //redraw_flag = 1;
    }
    if (GET_BIT(reg[REG_LCDC], LCDC_OBJ_ENABLE)) {
        draw_sprites(y);
    }
}

// Draws the Background & Window
void draw_tiles(u8 y) {
    u16 y_screen_width = y * SCREEN_WIDTH;
    
    u8  sx      = reg[REG_SCX];
    u8  sy      = reg[REG_SCY];
    u8  wx      = (reg[REG_WX] - 7) & 0xFF;
    u8  wy      = reg[REG_WY];

    u8  td_area_flag    = GET_BIT(reg[REG_LCDC], LCDC_BGW_TILEDATA_AREA);
    u16 vram_offset     = MEM_VRAM;

    // Drawing optimization
   // int tile_index_prev = -1;
   // u8  row_prev = 0;

    u8  window_in_line  = 0;
    u8  bg_in_line = 1;

    u8  byte1 = 0, byte2 = 0;
    u8  row;
    
    u16 bg_tm_area;;
    u16 bg_y; // translate the background coordinates to the screen ((row / 8) * 32)
    u8  bg_row;

    u16 win_tm_area;
    u16 win_y; // translate the window coordinates to the screen
    u8  win_row;

    u8  color_index;
    u16 td_addr, pixel_offset;

    /*
    u16 tile_index_changes = 0;
    u16 tm_addr_changes = 0;
    u16 bg_pixels = 0;
    u16 win_pixels = 0;
    u16 pixels_updates = 0;
    */

    //printf("%d,", wy);
    // Check if window is enabled and visible at this scanline
    if (GET_BIT(reg[REG_LCDC], LCDC_W_ENABLE) && wy <= y && wx < SCREEN_WIDTH) {
        window_in_line = 1;

        win_tm_area = GET_BIT(reg[REG_LCDC], LCDC_W_TILEMAP_AREA) ? 0x9C00 : 0x9800;
        win_y       = ( ((y - wy) & 0xFF) >> 3) << 5;
        win_row     = ((y - wy) & 0xFF) % 8;

        // Check if window covers the entire background
        if (wx <= 0 && wy <= 0) {
            bg_in_line = 0;
        }
    }
    if (bg_in_line) {
        bg_tm_area      = GET_BIT(reg[REG_LCDC], LCDC_BG_TILEMAP_AREA) ? 0x9C00 : 0x9800;
        bg_y    = ( ((y + sy) & 0xFF) >> 3) << 5; // translate the background coordinates to the screen ((row / 8) * 32)
        bg_row  = ((y + sy) & 0xFF) % 8;
    }

    for (u8 x = 0; x < SCREEN_WIDTH; x++) {
        u8 col;
        u16 tm_addr; 
        u16 xpos, ypos;
        u16 pixel_pos;
        int tile_index;
        
        // Check whether to display the background or the window
        if (window_in_line && x >= wx) {
            ypos = win_y;
            xpos = ( (x - wx) & 0xFF) >> 3; // (x / 8);

            row = win_row;
            col = ((x - wx) & 0xFF) % 8;

            tm_addr = win_tm_area + ypos + xpos;

            //win_pixels++;
        }
        else {
            ypos = bg_y;
            xpos = ( (x + sx) & 0xFF) >> 3;

            row = bg_row;
            col = ((x + sx) & 0xFF) % 8;

            tm_addr = bg_tm_area + ypos + xpos;

            //bg_pixels++;
        }

        // Only fetch new bytes when the tilemap address has changed
        if (tm_addr_prev != tm_addr) {
            tm_addr_prev = tm_addr;
            if (td_area_flag) tile_index = vram[tm_addr - vram_offset];
            else tile_index = (s8)vram[tm_addr - vram_offset];

            // Get tile data (unsigned/signed addressing)
            // (0-127 are in block 2, -128 to -1 are in block 1)
            td_addr = (td_area_flag ? 0x8000 : 0x9000) + (tile_index * 16);

            // Get pixel info
            pixel_offset = td_addr + (row * 2) - vram_offset;
            byte1 = vram[pixel_offset];      // represents lsb of the color_index of each pixel
            byte2 = vram[pixel_offset + 1];  // represents msb of the color_index of each pixel
        }
        // get color of pixel using the 2BPP method
        color_index = (GET_BIT(byte2, 7 - col) << 1) | GET_BIT(byte1, 7 - col);

        // Update pixel color
        pixel_pos = x + y_screen_width;
        if (background_buffer[pixel_pos] != pal_bgp[color_index]) {
            background_buffer[pixel_pos] = pal_bgp[color_index];
            // Tell screen to redraw at the next step
            if (!redraw_flag) redraw_flag = 1;

            //pixels_updates++;
        }
        
        /*
        if (y == 60) {
            printf("ti: %d, tm: %d, bg: %d, win: %d, pixels: %d\n",
                tile_index_changes, tm_addr_changes, bg_pixels, win_pixels, pixels_updates);
        }
        */
            
    }
}

void draw_sprites(u8 y) {
    if (object_count == 0) return;

    u16 y_screen_width = y * SCREEN_WIDTH;

    u8 lcdc = reg[REG_LCDC];
    u8 obj_height = GET_BIT(lcdc, LCDC_OBJ_SZ) ? 16 : 8; // 8x16 sprites

    // iterate through all sprites, draw if pixel intercects with our scanline
    for (s8 obj = object_count-1; obj >= 0; obj--) {
        u16 index       = objects[obj]; // spr * 4
        u8 ypos         = oam[index    ];
        u8 xpos         = oam[index + 1];
        u8 tile_index   = oam[index + 2];
        u8 attr         = oam[index + 3];
        u8 palette_no, flip_x, flip_y, bg_over_obj;
        u8 pixel_updated = 0;

        u8 byte1, byte2;
        u8 color_index;
        u16 pixel_offset;
        u16 pixel_pos;
        short ty; // the sprite line to draw

        u8* palette;

        // check if outside screen
        if (xpos == 0 || xpos >= 168) { continue; }
        
        ty = (y - (ypos - 16));
        flip_y = GET_BIT(attr, OAM_Y_FLIP);
        if (flip_y) ty = obj_height - ty;

        palette_no  = GET_BIT(attr, OAM_PALLETE_DMG);
        flip_x      = GET_BIT(attr, OAM_X_FLIP);
        bg_over_obj = GET_BIT(attr, OAM_BG_OVER_OBJ);
        palette = palette_no == 0 ? pal_obp0 : pal_obp1;

        // get pixel info
        pixel_offset = (tile_index * 16) + (ty * 2); // each tile takes 16 bytes (8x8x2BPP), each row of pixels is 2 bytes (2BPP)
        byte1 = vram[pixel_offset];      // represents lsb of the color_index of each pixel
        byte2 = vram[pixel_offset + 1];  // represents msb of the color_index of each pixel

        for (s8 x = 7; x >= 0; x--) {
            u8 px = (xpos - 8) + (flip_x ? x : (7 - x)); // Horizontal flip
            
            if (px >= SCREEN_WIDTH || px < 0) continue; // boundary check

            color_index = (GET_BIT(byte2, x) << 1) | GET_BIT(byte1, x);

            // Transparent pixel
            if (color_index == 0) continue;

            // Update pixel color
            pixel_pos = px + y_screen_width;

            // BG and Window colors 1-3 over the OBJ
            if (bg_over_obj && background_buffer[pixel_pos] != 0) continue;

            if (background_buffer[pixel_pos] != palette[color_index]) {
                background_buffer[pixel_pos] = palette[color_index];
                // Tell screen to redraw at the next step
                if (!redraw_flag) redraw_flag = 1;
            }
        }

    }
}
