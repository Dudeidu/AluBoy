#include "ppu.h"
#include "macros.h"

#include <stdio.h>
#include "emu_shared.h"

#define PIXELS_PER_BYTE 1
#define BITS_PER_PIXEL  8

u8* sprite_buffer      = NULL;
u8* background_buffer  = NULL;
u8* window_buffer      = NULL;

u8 redraw_flag;

u16 scanline_counter; // every >SCANLINE_DOTS, update LY

// Drawing optimization
u16 tm_addr_prev        = 0;
int tile_index_prev     = 1;

// FORWARD DECLARE
void draw_scanline(u8 y);
void draw_tiles(u8 y);
void draw_sprites(u8 y);

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

// Whether the screen needs to be redrawn
u8 ppu_get_redraw_flag() {
    return redraw_flag;
}
void ppu_set_redraw_flag(u8 val) {
    redraw_flag = val;
}

void ppu_update(u8 cycles)
{
    u8 clock = cycles;

    //printf("%d,", reg[REG_STAT]);
    scanline_counter += clock;
    // Reached end of scanline
    if (scanline_counter > SCANLINE_DOTS) {
        scanline_counter -= SCANLINE_DOTS;

        // Check for coincidence interrupt (if LYC=LY and interrupt is enabled)
        reg[REG_LYC] = (reg[REG_LY] == reg[REG_LYC]);
        if (reg[REG_LYC] && GET_BIT(reg[REG_STAT], STAT_INT_LYC))
        {
            // request interrupt
            SET_BIT(reg[REG_IF], INT_BIT_STAT);
        }
        // Move to a new scanline
        reg[REG_LY] ++;
        if (reg[REG_LY] >= 0x9A) reg[REG_LY] = 0;

        // VBlank
        if (reg[REG_LY] == SCREEN_HEIGHT) {
            //printf("VBlank start...\n");
            // change lcd mode to vblank
            //SET_BIT(reg[REG_STAT], LCD_MODE_VBLANK);

            // Vblank interrupt request
            SET_BIT(reg[REG_IF], INT_BIT_VBLANK);

            // request interrupt if enabled
            if (GET_BIT(reg[REG_STAT], STAT_INT_VBLANK)) {
                SET_BIT(reg[REG_IF], INT_BIT_STAT);
            }
        }
        // HBlank
        else if (reg[REG_LY] < SCREEN_HEIGHT) {
            // change lcd mode to hblank
            //SET_BIT(reg[REG_STAT], LCD_MODE_HBLANK);
            // request interrupt if enabled
            if (GET_BIT(reg[REG_STAT], STAT_INT_HBLANK)) {
                SET_BIT(reg[REG_IF], INT_BIT_STAT);
            }

            // HBLANK HDMA

            // DEBUG Draw entire line //////////////////////////////
            draw_scanline(reg[REG_LY] == 0 ? (SCREEN_HEIGHT - 1) : (reg[REG_LY] - 1));
        }
        //printf("%d,", reg[REG_LY]);
    }
    // OAM access

    //
    else {
        // get pixel coordinate

    }
    // ...
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

void draw_scanline(u8 y) {
    if (GET_BIT(reg[REG_LCDC], LCDC_BGW_ENABLE)) {
        draw_tiles(y);
    }
    if (GET_BIT(reg[REG_LCDC], LCDC_OBJ_ENABLE)) {
        draw_sprites(y);
    }
}

// Draws the Background & Window
void draw_tiles(u8 y) {
    u8  sx      = reg[REG_SCX];
    u8  sy      = reg[REG_SCY];
    u8  wx      = reg[REG_WX];
    u8  wy      = reg[REG_WY];

    u8  td_area_flag    = GET_BIT(reg[REG_LCDC], LCDC_BGW_TILEDATA_AREA);
    u16 bg_tm_area      = GET_BIT(reg[REG_LCDC], LCDC_BG_TILEMAP_AREA) ? 0x9C00 : 0x9800;
    u16 win_tm_area     = GET_BIT(reg[REG_LCDC], LCDC_W_TILEMAP_AREA) ? 0x9C00 : 0x9800;
    u16 vram_offset     = MEM_VRAM;

    u8  window_in_line = 0;
    u8  byte1 = 0, byte2 = 0;
    u8  pixel_updated = 0;
    
    u16 bg_y    = ( ((y + sy) & 0xFF) >> 3) << 5; // translate the background coordinates to the screen ((row / 8) * 32)
    u16 win_y   = ( ((wy - y) & 0xFF) >> 3) << 5; // translate the window coordinates to the screen
    u8  row     = y % 8;

    u8  color_index;
    u16 td_addr, pixel_offset;

    // Check if window is enabled and visible at this scanline
    if (GET_BIT(reg[REG_LCDC], LCDC_W_ENABLE) && wy <= y) {
        window_in_line = 1;
    }

    for (u8 x = 0; x < SCREEN_WIDTH; x++) {
        u8 col = x % 8;
        u16 tm_addr; 
        u16 xpos, ypos;
        int tile_index;
        
        // Check whether to display the background or the window
        if (window_in_line && x >= wx) {
            ypos = win_y;
            xpos = ( (x - wx) & 0xFF) >> 3; // (x / 8);
            tm_addr = win_tm_area + ypos + xpos;
        }
        else {
            ypos = bg_y;
            xpos = ( (x + sx) & 0xFF) >> 3;
            tm_addr = bg_tm_area + ypos + xpos;
        }

        // Only fetch new bytes when the tile index / tilemap address has changed
        if (tm_addr_prev != tm_addr) {
            tm_addr_prev = tm_addr;
            if (td_area_flag) tile_index = vram[tm_addr - vram_offset];
            else tile_index = (s8)vram[tm_addr - vram_offset] + 128;

            if (tile_index != tile_index_prev) {
                tile_index_prev = tile_index;
                // Get tile data (unsigned/signed addressing)
                // (0-127 are in block 2, -128 to -1 are in block 1)
                td_addr = (td_area_flag ? 0x8000 : 0x9000) + (tile_index * 16);

                // Get pixel info
                pixel_offset = td_addr + (row * 2) - vram_offset;
                byte1 = vram[pixel_offset];      // represents lsb of the color_index of each pixel
                byte2 = vram[pixel_offset + 1];  // represents msb of the color_index of each pixel
            }
        }
        color_index = (GET_BIT(byte2, 7 - col) << 1) | GET_BIT(byte1, 7 - col);

        pixel_updated = set_pixel(x + y * SCREEN_WIDTH, color_index);
        // Tell screen to redraw at the next step
        if (!redraw_flag && pixel_updated) redraw_flag = 1;
    }
}

void draw_sprites(u8 y) {
    u8 lcdc = reg[REG_LCDC];
    u8 is_big = GET_BIT(lcdc, LCDC_OBJ_SZ); // 8x16 sprites
    u8 height = is_big ? 16 : 8;

    // iterate through all sprites, draw if pixel intercects with our scanline
    for (u8 spr = 0; spr < 40; spr++) {
        u8 index        = spr << 2; // spr * 4
        u8 ypos         = oam[index    ];
        u8 xpos         = oam[index + 1];
        u8 tile_index   = oam[index + 2];
        u8 attr         = oam[index + 3];
        u8 flip_x, flip_y;

        u8 pixel_updated = 0;

        u8 byte1, byte2;
        u8 color_index;
        u16 pixel_offset;
        short ty = (y - (ypos - 16)); // the sprite line to draw

        // check if outside screen
        if (ypos == 0 || ypos >= 160) { continue; }
        if (xpos == 0 || xpos >= 168) { continue; }

        // check if intersecting
        if (ty < 0 || ty >= height) continue;

        flip_x = GET_BIT(attr, OAM_X_FLIP);
        flip_y = GET_BIT(attr, OAM_Y_FLIP);

        if (flip_y) ty = height - ty;
        
        // get pixel info
        pixel_offset = (tile_index * 16) + (ty * 2); // each tile takes 16 bytes (8x8x2BPP), each row of pixels is 2 bytes (2BPP)
        byte1 = vram[pixel_offset];      // represents lsb of the color_index of each pixel
        byte2 = vram[pixel_offset + 1];  // represents msb of the color_index of each pixel

        for (s8 x = 7; x >= 0; x--) {
            u8 px = (xpos - 8) + (flip_x ? x : (7 - x)); // Horizontal flip
            if (px >= SCREEN_WIDTH || px < 0) continue; // boundary check

            color_index = (GET_BIT(byte2, x) << 1) | GET_BIT(byte1, x);
            if (color_index == 0) continue; // white is transparent for sprites
            pixel_updated = set_pixel(px + (y * SCREEN_WIDTH), color_index);
            
            // tell screen to redraw at the next step
            if (!redraw_flag && pixel_updated) redraw_flag = 1;
        }

    }
}


/*
/// <summary>
/// Draws an 8x8 pixels tile to a buffer
/// </summary>
/// <param name="x">screen coordinate</param>
/// <param name="y">screen coordinate</param>
/// <param name="tile_data">pointer to the tile data</param>
void ppu_draw_tile(int x, int y, u8* tile_data, BufferType buffer_type)
{
    // Ensure the provided x and y coordinates are within bounds.
    // Add error handling as needed.
    
    // Get the target buffer
    uint8_t* buffer = NULL;
    switch (buffer_type)
    {
        case SPRITE_BUFFER:     buffer = sprite_buffer; break;
        case BACKGROUND_BUFFER: buffer = background_buffer; break;
        case WINDOW_BUFFER:     buffer = window_buffer; break;
        default:
            // Handle unknown buffer_type value or print an error message
            fprintf(stderr, "Unknown buffer_type: %d\n", buffer_type);
            return;  // Or handle appropriately
    }
    // Tiles are 16 bytes long, each line is represented by 2 bytes.
    for (u8 r = 0; r < 8; r++) // Iterate through each line
    {
        u8 byte1 = tile_data[r * 2];      // represents lsb of the color_index of each pixel
        u8 byte2 = tile_data[r * 2 + 1];  // represents msb of the color_index of each pixel

        // The color index of pixel c in line r
        for (u8 c = 0; c < 8; c++)
        {
            u8 color_index = (GET_BIT(byte2, 7 - c) << 1) | GET_BIT(byte1, 7 - c);
            // 0 = transparent in objects (not drawn)
            if (color_index == 0 && buffer_type == SPRITE_BUFFER) continue;

            //buffer[(y + r) * SCREEN_WIDTH + x + c] = color_index;
        }
    }
}
*/