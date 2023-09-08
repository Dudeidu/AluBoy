#include "ppu.h"
#include "macros.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include "emu_shared.h"
#include "cgb_palettes.h"

#include "gb.h"
#include "mmu.h"
#include "input.h"
#include "timer.h"


#define PIXELS_PER_BYTE 1
#define BITS_PER_PIXEL  8

typedef struct {
    u8 index;
    u8 priority;
} ObjectPriority;

RGBColor pal_monochrome[4] = {
    {245, 250, 239},   // white (greenish)
    {134, 194, 112},   // light
    {47, 105, 87},     // dark
    {0, 0, 0},         // black
};
RGBColor pal_bgp[4] = {
    {245, 250, 239},   // white (greenish)
    {134, 194, 112},   // light
    {47, 105, 87},     // dark
    {0, 0, 0},         // black
};
RGBColor pal_obp0[4] = {
    {245, 250, 239},   // white (greenish)
    {134, 194, 112},   // light
    {47, 105, 87},     // dark
    {0, 0, 0},         // black
};
RGBColor pal_obp1[4] = {
    {245, 250, 239},   // white (greenish)
    {134, 194, 112},   // light
    {47, 105, 87},     // dark
    {0, 0, 0},         // black
};
RGBColor pal_cgb[16][4]; // palettes 0-7 for BG, 8-15 for OBJ
u8 cram[128];           // (cgb) color/palette RAM

// Color and Palette data
RGBColor *lcd_pixels;   // final result to be displayed onto the screen
u8* lcd_buffer; 
u8* lcd_index_buffer;   // internal buffer that keeps the color index (instead of the palette index)
u8  pal_bgp_index[4]      = { 0, 1, 2, 3 };
u8  pal_obp0_index[4]     = { 0, 1, 2, 3 };
u8  pal_obp1_index[4]     = { 0, 1, 2, 3 };

u8  cgb_bg_pal_inc_flag;// whether to increment the palette index after writing to BGPD
u8  cgb_bg_pal_addr;    // copy of palette index (bits 0-5 of BGPI)

u8  cgb_obj_pal_inc_flag; // whether to increment the palette index after writing to OBPD
u8  cgb_obj_pal_addr;     // copy of palette index (bits 0-5 of OBPI)

u8 cgb_object_priority_mode;

u8 redraw_flag;         // when true screen will be redrawn at the end of the frame

u8  vram_accessible;    // (unused) whether the VRAM is accessible for reading (PPU only)

u16 scanline_counter;   // every >SCANLINE_DOTS, update LY
u8  window_line;        // window LY
u16 lcd_mode_next;      // determines after what cycle (scanline_counter) to switch lcd_mode

u8 sx_start;            // the value of SCX register that was read at the start of the scanline

ObjectPriority objects[40]; // array of object indexes that intersect with of the current scanline
u8  object_count;       // how many objects to read from the array

// Drawing optimization
int vblank_counter  = 0;
u16 tm_addr_prev    = 0;


// Debugging
debug_show_line_data = 0;

// FORWARD DECLARE
void draw_scanline(u8);
void draw_tiles(u8);
void draw_objects(u8);
u8   search_oam(u8);
void check_lyc();
void check_stat_irq(u8);
void update_palette(u8, u8);
void disable_lcd();
void enable_lcd();
u16  calculate_mode3_duration();
inline void switch_lcd_mode(enum LCDMode);
inline u8 compare_rgb_colors(RGBColor, RGBColor, u8);
void update_compatibility_palettes(u8 hash, RGBColor* bgp, RGBColor* obp0, RGBColor* obp1);

// PUBLIC --------------------------------------------------

// Initialize
int ppu_init()
{
    // Allocate memory for the buffer and initialize with 0
    int buffer_size = (SCREEN_WIDTH * SCREEN_HEIGHT) / PIXELS_PER_BYTE;

    lcd_index_buffer = NULL;
    lcd_index_buffer = (u8*)calloc(buffer_size, sizeof(u8));
    if (lcd_index_buffer == NULL)
    {
        fprintf(stderr, "Failed to allocate memory for the LCD index buffer!\n");
        return 0;
    }

    lcd_pixels = NULL;
    // Allocate memory for the array
    lcd_pixels = (RGBColor *)calloc(buffer_size, sizeof(RGBColor));
    if (lcd_pixels == NULL)
    {
        fprintf(stderr, "Failed to allocate memory for the LCD pixels buffer!\n");
        return 0;
    }

    return 1;
}
void ppu_powerup()
{
    lcd_mode = LCD_MODE_VBLANK;
    lcd_enabled      = 1;
    lcd_mode_next = SCANLINE_DOTS;
    vram_accessible = 1;

    stat_irq_flag = 0;
    stat_bug = 0;

    redraw_flag = 1;

    reg[REG_LCDC] = 0x91;
    reg[REG_STAT] = 0x85;

    reg[REG_SCY] = 0x00;
    reg[REG_SCX] = 0x00;
    reg[REG_LY] = 0x00;
    reg[REG_LYC] = 0x00;

    reg[REG_DMA] = cgb_mode ? 0x00 : 0xFF;
    reg[REG_BGP] = 0xFC;
    reg[REG_OBP0] = 0xFF;
    reg[REG_OBP1] = 0xFF;

    reg[REG_WY] = 0x00;
    reg[REG_WX] = 0x00;

    reg[REG_BGPI] = 0xFF;
    reg[REG_BGPD] = 0xFF;
    reg[REG_OBPI] = 0xFF;
    reg[REG_OBPD] = 0xFF;

    reg[REG_OPRI] = 0x00;
    cgb_object_priority_mode = 0;

    // (cgb) Initialize the colors in the array
    memset(cram, 0, sizeof(cram));
    for (int i = 0; i < 16; i++) {
        for (int j = 0; j < 4; j++) {
            pal_cgb[i][j].red   = 255;
            pal_cgb[i][j].green = 255;
            pal_cgb[i][j].blue  = 255;
        }
    }

    if (gb_cgb_compability_palette_flag) {
        update_compatibility_palettes(palette_hash_id, pal_bgp, pal_obp0, pal_obp1);
    }
}

// Used for passing the pixel buffer to GL
RGBColor* ppu_get_pixel_buffer() {
    return lcd_pixels;
}

// Whether the screen needs to be redrawn
u8 ppu_get_redraw_flag() {
    // even if true, dont draw when frame is skipped
    return redraw_flag && (vblank_counter % gb_frameskip == 0);
}
void ppu_set_redraw_flag(u8 val) {
    redraw_flag = val;
}


u8 ppu_read_register(u8 reg_id) {
    // handles special cases
    switch (reg_id) {
        case REG_BGPD:
        case REG_OBPD:
            if (lcd_mode == LCD_MODE_VRAM) return 0xFF;
            return reg[reg_id];

        default:
            return reg[reg_id];
    }
}
void ppu_write_register(u8 reg_id, u8 value) {
    // handles special cases
    switch (reg_id) {
        case REG_LCDC:
            if (GET_BIT(reg[REG_LCDC], 7) && lcd_mode != LCD_MODE_VBLANK && !GET_BIT(value, 7)) {
                printf("illegal LCD disable\n");
            }
            if (GET_BIT(reg[REG_LCDC], 7) && !GET_BIT(value, 7)) {
                disable_lcd();
            }
            else if (!GET_BIT(reg[REG_LCDC], 7) && GET_BIT(value, 7)) {
                enable_lcd();
            }
            reg[REG_LCDC] = value;
            
            break;
        case REG_STAT:
            // STAT irq blocking / bug
            // On DMG, a STAT write causes all sources to be enabled (but not necessarily active) for one cycle.
            // call your STAT IRQ poll function, then set STAT enable flags to their true values.
                            
            // TODO the LYC coincidence interrupt appears to be delayed by 1 cycle after Mode 2, 
            // so it does not block if Mode 0 is enabled as well.
            reg[REG_STAT] = (reg[reg_id] & 7) | (value & 0x78) | 0x80; // bits 0-2 are read only, bit 7 is unused
            if (lcd_enabled && !cgb_mode) {
                stat_bug = 1;
                check_stat_irq(0);
                stat_bug = 0;
            }
            break;
        case REG_SCY:
            reg[reg_id] = value;
            break;
        case REG_SCX:
            reg[reg_id] = value;
            break;
        case REG_LY: 
            if (lcd_enabled) reg[reg_id] = 0;
            break;
        case REG_LYC:
            reg[reg_id] = value;

            if (lcd_enabled) {
                check_lyc();
                check_stat_irq(0);
            }
            break;
        case REG_DMA:
            // Source:      $XX00-$XX9F   ;XX = $00 to $DF
            // Destination: $FE00-$FE9F
            reg[reg_id] = value;
            oam_dma_transfer_flag = 1;
            break;
        case REG_BGP:
            if (cgb_mode) update_palette(REG_BGP, value);
            reg[reg_id] = value;
            break;
        case REG_OBP0:
            if (cgb_mode) update_palette(REG_OBP0, value);
            reg[reg_id] = value;
            break;
        case REG_OBP1:
            if (cgb_mode) update_palette(REG_OBP1, value);
            reg[reg_id] = value;
            break;
        case REG_WY:
            reg[reg_id] = value;
            break;
        case REG_WX:
            reg[reg_id] = value;
            break;
        case REG_VBK:
            if (cgb_mode)   reg[reg_id] = (value & 1) | (~1); // only bit 0 matters
            else            reg[reg_id] = 0xFF;
            break;
        case REG_HDMA1:
        case REG_HDMA2:
        case REG_HDMA3:
        case REG_HDMA4:
        case REG_HDMA5:
            if (cgb_mode)   reg[reg_id] = value;
            else            reg[reg_id] = 0xFF;
            break;
        case REG_BGPI:
            if (cgb_mode) {
                reg[reg_id]         = value | 0x40;         // bit 6 is unused
                cgb_bg_pal_inc_flag = GET_BIT(value, 7);    // bit 7
                cgb_bg_pal_addr     = value & 0x3F;         // bits 0-5
            }
            else reg[reg_id] = 0xFF;
            break;
        case REG_BGPD:
            /*
            Bit 0-4   Red Intensity   ($00-1F)
            Bit 5-9   Green Intensity ($00-1F)
            Bit 10-14 Blue Intensity  ($00-1F)
            */
            if (cgb_mode) {
                // Get the corresponding palette
                u8 palette_index    = cgb_bg_pal_addr >> 3; // 2 bytes/color * 4 colors/palette
                u8 color_index      = (cgb_bg_pal_addr - (palette_index << 3)) / 2; // the color index (out of 4) in this palette
                u8 color_byte       = cgb_bg_pal_addr % 2;  // 0 = 5 red + 3 lower bits of green, 1 = 2 high bits of green + 5 blue + 1 unused bit
                RGBColor* pal       = &pal_cgb[palette_index][color_index];

                if (lcd_mode != LCD_MODE_VRAM) {
                    cram[cgb_bg_pal_addr] = value;
                    if (color_byte == 0) {
                        pal->red = value & 0x1F;                                // bit 0-4   red intensity
                        pal->green = (pal->green & (~7)) | ((value >> 5));      // bit 5-7   low 3 bits of green
                    }
                    else {
                        pal->blue = (value >> 2) & 0x1F;                        // Bit 10-14 (2-6)   blue intensity
                        pal->green = (pal->green & (7)) | ((value & 3) << 3);   // bit 8-9   (0-1)   high 2 bits of green
                    }
                }

                // Auto increment on write
                if (cgb_bg_pal_inc_flag) {
                    cgb_bg_pal_addr = (cgb_bg_pal_addr + 1) & 0x3F;     // wrap around when above 64
                    reg[REG_BGPI]   = (reg[REG_BGPI] & 0xC0) | cgb_bg_pal_addr;
                }
                reg[reg_id] = value;
            }
            else reg[reg_id] = 0xFF;
            break;
        case REG_OBPI:
            if (cgb_mode) {
                reg[reg_id]          = value | 0x40;        // bit 6 is unused
                cgb_obj_pal_inc_flag = GET_BIT(value, 7);   // bit 7
                cgb_obj_pal_addr     = value & 0x3F;        // bits 0-5
            }
            else reg[reg_id] = 0xFF;
            break;
        case REG_OBPD:
            /*
            Bit 0-4   Red Intensity   ($00-1F)
            Bit 5-9   Green Intensity ($00-1F)
            Bit 10-14 Blue Intensity  ($00-1F)
            */
            if (cgb_mode) {
                // Get the corresponding palette
                u8 palette_index    = (cgb_obj_pal_addr >> 3); // 2 bytes/color * 4 colors/palette
                u8 color_index      = (cgb_obj_pal_addr - (palette_index << 3)) / 2; // the color index (out of 4) in this palette
                u8 color_byte       = cgb_obj_pal_addr % 2;  // 0 = 5 red + 3 lower bits of green, 1 = 2 high bits of green + 5 blue + 1 unused bit
                RGBColor* pal       = &pal_cgb[palette_index + 8][color_index];

                if (lcd_mode != LCD_MODE_VRAM) {
                    cram[cgb_obj_pal_addr + 64] = value;
                    if (color_byte == 0) {
                        pal->red = value & 0x1F;                                // bit 0-4   red intensity
                        pal->green = (pal->green & (~7)) | ((value >> 5));      // bit 5-7   low 3 bits of green
                    }
                    else {
                        pal->blue = (value >> 2) & 0x1F;                        // Bit 10-14 (2-6)   blue intensity
                        pal->green = (pal->green & (7)) | ((value & 3) << 3);   // bit 8-9   (0-1)   high 2 bits of green
                    }
                }

                // Auto increment on write
                if (cgb_obj_pal_inc_flag) {
                    cgb_obj_pal_addr = (cgb_obj_pal_addr + 1) & 0x3F;     // wrap around when above 64
                    reg[REG_OBPI] = (reg[REG_OBPI] & 0xC0) | cgb_obj_pal_addr;
                }
                reg[reg_id] = value;
            }
            else reg[reg_id] = 0xFF;
            break;

        case REG_OPRI:
            if (cgb_mode) {
                cgb_object_priority_mode = value | 0xFE;
                reg[reg_id] = value | 0xFE;
            }
            else            reg[reg_id] = 0xFF;
            break;


        // Unused registers
        default:
            reg[reg_id] = 0xFF;
            break;
    }
}


void oam_dma_transfer_tick() {
    u8 t_u8;
    u16 addr = ((reg[REG_DMA] << 8) | oam_dma_index) & 0xFFFF;

    oam_dma_access_flag = 1;
    t_u8 = read(addr);
    oam_dma_access_flag = 0;

    oam[oam_dma_index] = t_u8;

    if (++oam_dma_index > 0x9F) {
        oam_dma_index = 0;
        oam_dma_transfer_flag = 0;
    }
}

void ppu_tick()
{
    u8 clock = M_CYCLE;

    if (!lcd_enabled) return;

    scanline_counter += clock;
    if (scanline_counter < lcd_mode_next) return;
    
    //printf("%d\n", lcd_mode);
    // Switch lcd mode

    // Pan Docs:
    // The following are typical when the display is enabled:
    //   Mode 2  2_____2_____2_____2_____2_____2___________________2____
    //   Mode 3  _33____33____33____33____33____33__________________3___
    //   Mode 0  ___000___000___000___000___000___000________________000
    //   Mode 1  ____________________________________11111111111111_____

    switch (lcd_mode) {
        case LCD_MODE_OAM: // Searching OAM for OBJs whose Y coordinate overlap this line
            object_count = search_oam(reg[REG_LY]);

            switch_lcd_mode(LCD_MODE_VRAM);
            lcd_mode_next += calculate_mode3_duration();

            // TODO figure out stat irq blocking
            // Dirty fix that mostly works for some reason
            //stat_irq_flag = 0;

            vram_accessible = 1;
            break;

        case LCD_MODE_VRAM: // Reading OAM and VRAM to generate the picture
            switch_lcd_mode(LCD_MODE_HBLANK);
            lcd_mode_next = SCANLINE_DOTS;

            vram_accessible = 0;

            // request stat interrupt if enabled
            check_stat_irq(0);

            // Draw scanline //////////////////////////////
            if (lcd_enabled && vblank_counter % gb_frameskip == 0) {
                draw_scanline(reg[REG_LY]);
            }

            break;

        case LCD_MODE_HBLANK:
            scanline_counter -= SCANLINE_DOTS;

            // Move to a new scanline
            reg[REG_LY]++;

            check_lyc();
            check_stat_irq(0);

            // Update inputs at a different LY each frame to avoid detection
            if (!input_updated) {
                input_joypad_update();
            }

            if (reg[REG_LY] == SCREEN_HEIGHT) {
                // Vblank period start
                vblank_counter++;

                switch_lcd_mode(LCD_MODE_VBLANK);
                lcd_mode_next = SCANLINE_DOTS;
                
                // request stat interrupt if enabled
                check_stat_irq(1);
                // Vblank interrupt request
                SET_BIT(reg[REG_IF], INT_BIT_VBLANK);

            }
            else {
                // change lcd mode to oam search (0~80)
                switch_lcd_mode(LCD_MODE_OAM);
                lcd_mode_next = 80;

                // request stat interrupt if enabled
                check_stat_irq(0);
            }
            break;

        case LCD_MODE_VBLANK:
            scanline_counter -= SCANLINE_DOTS;
            // Move to a new scanline
            reg[REG_LY] ++;

            check_lyc();
            check_stat_irq(0);
            
            // if reached last line
            if (reg[REG_LY] == 154) {
                // reset line
                reg[REG_LY] = 0;
                window_line = 0;

                // request interrupt if enabled
                check_lyc();
                check_stat_irq(0);
                
                // obscure behavior
                if (cgb_mode && !double_speed) {
                    vram_accessible = 0;
                }
                if (double_speed) {
                    vram_accessible = 1;
                }

                // reset input fetching
                input_updated = 0;

                if (!input_updated) {
                    input_joypad_update();
                }

                // change lcd mode to oam search (0~80)
                switch_lcd_mode(LCD_MODE_OAM);
                lcd_mode_next = 80;
                check_stat_irq(0);
            }
            break;
    }
    
}

void ppu_cleanup() {
    if (lcd_index_buffer)   free(lcd_index_buffer);
    if (lcd_pixels)         free(lcd_pixels);
}



// PRIVATE --------------------------------------------------

// assigns gray shades to the color indexes
void update_palette(u8 reg, u8 value) {
    u8* palette;
    switch (reg) {
        case REG_BGP:   palette = pal_bgp_index; break;
        case REG_OBP0:  palette = pal_obp0_index; break;
        case REG_OBP1:  palette = pal_obp1_index; break;
        default: return;
    }
    for (int i = 0; i < 4; i++) {
        palette[i] = (value >> (i * 2)) & 0x3;
    }
}

void update_compatibility_palettes(u8 hash, RGBColor* bgp, RGBColor* obp0, RGBColor* obp1) {
    u8 letter = read(ROM_TITLE + 3);
    u8 tbi = -1; // color table index
    u8 hl_pairs_size = sizeof(hash_letter_pairs) / sizeof(hash_letter_pairs[0]);
    unsigned int (*table)[3][4];

    // search the hash-letter pair and return its color table index
    for (int i = 0; i < hl_pairs_size; i++) {
        if ((hash_letter_pairs[i].hash == hash) 
            && (letter == hash_letter_pairs[i].letter || hash_letter_pairs[i].letter == '\0')) {
            tbi = hash_letter_pairs[i].tbi;
            break;
        }
    }
    // if not found
    if (tbi == -1) return;

    // lookup color table by index
    table = &color_tables[tbi];

    // update the palettes from color table

    // Background palette
    for (int i = 0; i < 4; i++) {
        bgp[i].red     = ((*table)[0][i] >> 16) & 0xFF;
        bgp[i].green   = ((*table)[0][i] >> 8) & 0xFF;
        bgp[i].blue    = (*table)[0][i] & 0xFF;
    }

    // Object Palette 0
    for (int i = 0; i < 4; i++) {
        obp0[i].red     = ((*table)[1][i] >> 16) & 0xFF;
        obp0[i].green   = ((*table)[1][i] >> 8) & 0xFF;
        obp0[i].blue    = (*table)[1][i] & 0xFF;
    }

    // Object Palette 1
    for (int i = 0; i < 4; i++) {
        obp1[i].red     = ((*table)[2][i] >> 16) & 0xFF;
        obp1[i].green   = ((*table)[2][i] >> 8) & 0xFF;
        obp1[i].blue    = (*table)[2][i] & 0xFF;
    }
}

void disable_lcd() {
    int buffer_size = (SCREEN_WIDTH * SCREEN_HEIGHT) / PIXELS_PER_BYTE;

    /*
    when the LCD is turned off: 
    automatically enter HBLANK, 
    clear bits 0 and 1 (background enable and object enable) of the LCDC register 
    set LY to 0.
    */

    lcd_enabled = 0;

    RESET_BIT(reg[REG_LCDC], LCDC_BGW_ENABLE);
    RESET_BIT(reg[REG_LCDC], LCDC_OBJ_ENABLE);
    reg[REG_LY] = 0;
    window_line = 0;

    vblank_counter = 0;
    scanline_counter = 0;

    switch_lcd_mode(LCD_MODE_HBLANK);
    lcd_mode_next = SCANLINE_DOTS;
    
    vram_accessible = 0;

    //memset(lcd_pixels, 255, buffer_size * sizeof(RGBColor));
    memset(lcd_index_buffer, 0, buffer_size);
}
void enable_lcd() {
    lcd_enabled = 1;
    vram_accessible = 1;
    check_lyc();
    check_stat_irq(0);
}

u16 calculate_mode3_duration() {
    u16 dur = 0;
    u8 wx = reg[REG_WX];
    u8 sx = reg[REG_SCX];

    
    if (wx == 166 || wx == 0xFF || !GET_BIT(reg[REG_LCDC], LCDC_W_ENABLE)) {
        // Total cycles: (173.5 + (xscroll % 7))
        dur += 6;                   // (6 cycles) fetch background tile nametable+bitplanes
        dur += 168 + (sx % 7);      // (167.5 + (xscroll % 7) cycles) fetch another tile and sprite window.                        
    }
    else if ( (wx > 0) && (wx < 166) ) {
        // Total cycles: (173.5 + 6 + (xscroll % 7))
        dur += 6;                   // (6 cycles) fetch background tile nametable+bitplanes
        dur += (sx % 7) + wx + 1;   // (1 to 172 cycles) ((xscroll % 7) + xwindow + 1)
        dur += 6;                   // (6 cycles) fetch window tile nametable+bitplanes
        dur += (167 - wx);          // (1.5 to 166.5 cycles)  (166.5 - xwindow)                       
    }
    else if (wx == 0) {
        if (sx % 7 == 7) {
            // Total cycles: 187.5
            dur += 7;                   // (7 cycles) technically the last B is part of the B01s pattern.
            dur += 6;                   // (6 cycles) fetch window tile nametable+bitplanes
            dur += 168 + 7;             // (174.5 cycles)
        }
        else {
            // Total cycles: 180.5 to 186.5
            dur += 7;                   // (7 cycles) technically the last B is part of the B01s pattern.
            dur += 6;                   // (6 cycles) fetch window tile nametable+bitplanes
            dur += 168 + (sx % 7);      // (167.5 to 173.5 cycles) (167.5 + (scroll % 7))                 
        }
    }
    // TODO better simulation of the duration that sprites add
    dur += (object_count * 10);

    return dur;
}

inline u8 compare_rgb_colors(RGBColor color1, RGBColor color2, u8 value_correction) {
    if (value_correction) {
        return color1.red == (((u16)color2.red * 255) / 31) 
            && color1.green == (((u16)color2.green * 255) / 31) 
            && color1.blue == (((u16)color2.blue * 255) / 31);

    }
    else
        return color1.red == color2.red && color1.green == color2.green && color1.blue == color2.blue;
}

// Custom comparison function for sorting objects
int compare_object_by_priority(const void* a, const void* b) {
    // Compare priorities for sorting
    ObjectPriority *obj_a = (ObjectPriority *)a;
    ObjectPriority *obj_b = (ObjectPriority *)b;
    
    if (obj_a->priority != obj_b->priority) {
        return obj_b->priority - obj_a->priority;
    } else {
        // If the priority values are the same, sort by index
        return obj_b->index - obj_a->index;
    }
}
int compare_object_by_index(const void* a, const void* b) {
    // Compare priorities for sorting
    ObjectPriority *obj_a = (ObjectPriority *)a;
    ObjectPriority *obj_b = (ObjectPriority *)b;
    
    return obj_b->index - obj_a->index;
}

// Request stat interrupt if source is enabled
void check_stat_irq(u8 vblank_start) {
    if  (stat_bug || 
        (lcd_mode == LCD_MODE_HBLANK && GET_BIT(reg[REG_STAT], STAT_INT_HBLANK) ) || // HBlank
        (lcd_mode == LCD_MODE_VBLANK && GET_BIT(reg[REG_STAT], STAT_INT_VBLANK) ) || // VBlank
        ((lcd_mode == LCD_MODE_OAM || vblank_start) && GET_BIT(reg[REG_STAT], STAT_INT_OAM) ) || // OAM
        (GET_BIT(reg[REG_STAT], 2)   && GET_BIT(reg[REG_STAT], STAT_INT_LYC)    ) // LYC
        ) {
        if (!stat_irq_flag) {
            SET_BIT(reg[REG_IF], INT_BIT_STAT);
            stat_irq_flag = 1;
        }
    }
    else {
        stat_irq_flag = 0;
    }
}

// Check for coincidence interrupt (if LYC=LY and interrupt is enabled)
void check_lyc() {
    if (reg[REG_LY] == reg[REG_LYC]) SET_BIT(reg[REG_STAT], 2);
    else RESET_BIT(reg[REG_STAT], 2);
}

// Searching OAM for OBJs whose Y coordinate overlap this scanline and stores them an a sorted array. returns the object amount
u8 search_oam(u8 y) {
    u8 count = 0;
    u8 obj_height = GET_BIT(reg[REG_LCDC], LCDC_OBJ_SZ) ? 16 : 8; // 8x16 sprites
    // iterate through all objects in OAM, count if pixel intersects with our scanline
    for (s8 obj = 0; obj < 40; obj++) {
        u8 index = obj << 2; // obj * 4
        u8 ypos = oam[index];
        u8 xpos = oam[index + 1];
        short ty; // the sprite line to draw

        // check if outside screen
        if ((ypos + obj_height) <= 16 || ypos >= 160) {
            continue;
        }
        // check if intersecting
        ty = (y - (ypos - 16));
        if (ty < 0 || ty >= obj_height) continue;

        // add to array of intersecting object indexes
        objects[count].index = index;
        objects[count].priority = xpos;
        count++;
        // count towards scanline limit (PPU only checks the Y coordinate to select objects)
        if (count == 10) break;
    }
    // sort the array based on the obj priority
    if (count > 0) {
        int (*compare_function) (const void*, const void*); // function pointer
        if (cgb_mode)
            compare_function = cgb_object_priority_mode ? compare_object_by_priority : compare_object_by_index;
        else 
            compare_function = compare_object_by_priority;

        qsort(objects, count, sizeof(ObjectPriority), compare_function);
    }
    return count;
}

inline void switch_lcd_mode(enum LCDMode mode) {
    reg[REG_STAT] = (reg[REG_STAT] & ~0x3) | mode;
    lcd_mode = mode;
}

void draw_scanline(u8 y) {
    if (debug_show_line_data) printf("line:%03d bg?:%d obj?:%d ", y, GET_BIT( reg[REG_LCDC], LCDC_BGW_ENABLE), GET_BIT(reg[REG_LCDC], LCDC_OBJ_ENABLE));
    
    if (cgb_mode || GET_BIT(reg[REG_LCDC], LCDC_BGW_ENABLE)) {
        draw_tiles(y);
    }
    else {
        // draw white color
        //memset(&lcd_buffer[y * SCREEN_WIDTH], 0, SCREEN_WIDTH);
        //redraw_flag = 1;
    }
    if (GET_BIT(reg[REG_LCDC], LCDC_OBJ_ENABLE)) {
        draw_objects(y);
    }
    if (debug_show_line_data) printf("\n");
}
void draw_tiles(u8 y) {
    // Draws the Background & Window
    
    u16 y_screen_width = y * SCREEN_WIDTH;
    
    u8  sx  = reg[REG_SCX];
    u8  sy  = reg[REG_SCY];
    u8  wx  = reg[REG_WX];
    u8  wy  = reg[REG_WY];

    u8  td_area_flag    = GET_BIT(reg[REG_LCDC], LCDC_BGW_TILEDATA_AREA);
    u16 vram_offset     = MEM_VRAM;

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
    RGBColor* palette = pal_bgp;

    // cgb
    u8  bgm_attr = 0; // BG map attributes
    u8  bg_to_oam_priority = 0;
    u8  flip_y = 0;
    u8  flip_x = 0;
    u8  tile_vram_bank = 0;
    u8  bg_palette_no = 0;

    // debug
    u8 bg_pixels = 0;
    u8 win_pixels = 0;

    // check if window is enabled and visible at this scanline
    if (GET_BIT(reg[REG_LCDC], LCDC_W_ENABLE) && wy <= y && wx < SCREEN_WIDTH + 7) {
        window_in_line = 1;

        win_tm_area = GET_BIT(reg[REG_LCDC], LCDC_W_TILEMAP_AREA) ? 0x9C00 : 0x9800;
        // get window position relative to screen 
        win_y       = ( (window_line & 0xFF) >> 3) << 5;
        win_row     = (window_line & 0xFF) % 8;
        window_line++;

        // check if window covers the entire background
        if (wx <= 7 && wy <= 0) {
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
        if (window_in_line && x + 7 >= wx) {
            ypos = win_y;
            xpos = ( (x - wx + 7) & 0xFF) >> 3; // (x / 8);

            row = win_row;
            col = ((x - wx + 7) & 0xFF) % 8;

            tm_addr = win_tm_area + ypos + xpos;

            win_pixels++;
        }
        else {
            ypos = bg_y;
            xpos = ( (x + sx) & 0xFF) >> 3;

            row = bg_row;
            col = ((x + sx) & 0xFF) % 8;

            tm_addr = bg_tm_area + ypos + xpos;

            bg_pixels++;
        }

        // Only fetch new bytes when the tilemap address has changed
        if (tm_addr_prev != tm_addr) {
            tm_addr_prev = tm_addr;
            if (td_area_flag) tile_index = vram[tm_addr - vram_offset];
            else tile_index = (s8)vram[tm_addr - vram_offset];

            // Get tile data (unsigned/signed addressing)
            // (0-127 are in block 2, -128 to -1 are in block 1)
            td_addr = (td_area_flag ? 0x8000 : 0x9000) + (tile_index * 16);

            // Get background attributes
            if (!cgb_mode) {
                // Get pixel info
                pixel_offset = td_addr + (row * 2) - vram_offset;
                byte1 = vram[pixel_offset];      // represents lsb of the color_index of each pixel
                byte2 = vram[pixel_offset + 1];  // represents msb of the color_index of each pixel
            }
            else {
                bgm_attr            = vram[tm_addr - vram_offset + BANKSIZE_VRAM];
                bg_palette_no       = bgm_attr & 7;
                tile_vram_bank      = GET_BIT(bgm_attr, 3);
                flip_x              = GET_BIT(bgm_attr, 5);
                flip_y              = GET_BIT(bgm_attr, 6);
                bg_to_oam_priority  = GET_BIT(bgm_attr, 7);
                
                // Get pixel info
                pixel_offset = td_addr + ((flip_y ? (7 - row) : row) * 2) - vram_offset;
                pixel_offset += tile_vram_bank * BANKSIZE_VRAM;
                byte1 = vram[pixel_offset];      // represents lsb of the color_index of each pixel
                byte2 = vram[pixel_offset + 1];  // represents msb of the color_index of each pixel

                // get cgb background palette
                palette = pal_cgb[bg_palette_no];
            }

        }
        // get color of pixel using the 2BPP method
        color_index = (GET_BIT(byte2, (flip_x ? col : (7 - col))) << 1) | GET_BIT(byte1, (flip_x ? col : (7 - col)));

        // Update pixel color
        pixel_pos = x + y_screen_width;

        RGBColor* col_lcd   = &lcd_pixels[pixel_pos];
        RGBColor* col_new   = &palette[cgb_mode ? color_index : pal_bgp_index[color_index]];
        if (!compare_rgb_colors(*col_lcd, *col_new, cgb_mode)) {
            if (cgb_mode) {
                col_lcd->red = ((u16)col_new->red * 255) / 31;
                col_lcd->green = ((u16)col_new->green * 255) / 31;
                col_lcd->blue = ((u16)col_new->blue * 255) / 31;
            }
            else {
                col_lcd->red = col_new->red;
                col_lcd->green = col_new->green;
                col_lcd->blue = col_new->blue;
            }

            lcd_index_buffer[pixel_pos] = color_index + (bg_to_oam_priority * 4);

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

    if (debug_show_line_data) printf("sx:%03d sy:%03d wx:%03d wy:%03d w_inline:%d bg_inline:%d winp:%d bgp:%d ", sx, sy, wx, wy, window_in_line, bg_in_line, win_pixels, bg_pixels);
}
void draw_objects(u8 y) {
    if (object_count == 0) return;

    u16 y_screen_width = y * SCREEN_WIDTH;

    u8 lcdc = reg[REG_LCDC];
    u8 obj_height = GET_BIT(lcdc, LCDC_OBJ_SZ) ? 16 : 8; // 8x16 sprites

    if (debug_show_line_data) printf("obj count: %02d ", object_count);

    // iterate through all sprites, draw if pixel intercects with our scanline
    for (s8 obj = 0; obj < object_count; obj++) {
        u16 index       = objects[obj].index; // spr * 4
        u8 ypos         = oam[index    ];
        u8 xpos         = oam[index + 1];
        // Bit 0 of tile index for 8x16 objects should be ignored
        u8 tile_index   = obj_height == 16 ? (oam[index + 2] & 0xFE) : oam[index + 2];
        u8 attr         = oam[index + 3];
        u8 palette_no, flip_x, flip_y, bg_over_obj;
        u8 tile_vram_bank = 0;

        u8 byte1, byte2;
        u8 color_index;
        u16 pixel_offset;
        u16 pixel_pos;
        short ty; // the sprite line to draw

        u8* palette_index;
        RGBColor* palette;

        // check if outside screen
        if (xpos >= 168 || xpos == 0) { continue; }
        
        ty = (y - (ypos - 16));
        flip_y = GET_BIT(attr, OAM_Y_FLIP);
        if (flip_y) ty = (obj_height - ty) - 1;

        
        flip_x      = GET_BIT(attr, OAM_X_FLIP);
        bg_over_obj = GET_BIT(attr, OAM_BG_OVER_OBJ);
        
        // get palette and pixel info
        // each tile takes 16 bytes (8x8x2BPP), each row of pixels is 2 bytes (2BPP)
        if (!cgb_mode) {
            palette_no  = GET_BIT(attr, OAM_PALLETE_DMG);
            palette = palette_no == 0 ? pal_obp0 : pal_obp1;
            palette_index = palette_no == 0 ? pal_obp0_index : pal_obp1_index;

            pixel_offset = (tile_index * 16) + (ty * 2); 
        }
        else {
            palette_no = attr & 7; // bits 0-2
            palette = pal_cgb[palette_no + 8];
            palette_index = pal_obp0_index; // unused

            tile_vram_bank = GET_BIT(attr, OAM_VRAM_BANK_CGB);
            pixel_offset = (tile_index * 16) + (ty * 2) + (tile_vram_bank * BANKSIZE_VRAM); 
        }
        

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
            //if (bg_over_obj && lcd_index_buffer[pixel_pos] != 0) continue;
            /*
            If the BG color index is 0, the OBJ will always have priority;
            Otherwise, if LCDC bit 0 is clear, the OBJ will always have priority;
            Otherwise, if both the BG Attributes and the OAM Attributes have bit 7 clear, the OBJ will have priority;
            Otherwise, BG will have priority.
            */
            
            if ((lcd_index_buffer[pixel_pos] % 4) == 0 ||
                !GET_BIT(reg[REG_LCDC], LCDC_BGW_ENABLE) ||
                (!bg_over_obj && lcd_index_buffer[pixel_pos] < 4))
            {
                RGBColor* col_lcd = &lcd_pixels[pixel_pos];
                RGBColor* col_new = &palette[cgb_mode ? color_index : palette_index[color_index]];
                if (!compare_rgb_colors(*col_lcd, *col_new, cgb_mode)) {
                    if (cgb_mode) {
                        col_lcd->red = ((u16)col_new->red * 255) / 31;
                        col_lcd->green = ((u16)col_new->green * 255) / 31;
                        col_lcd->blue = ((u16)col_new->blue * 255) / 31;
                    }
                    else {
                        col_lcd->red = col_new->red;
                        col_lcd->green = col_new->green;
                        col_lcd->blue = col_new->blue;
                    }

                    lcd_index_buffer[pixel_pos] = color_index;

                    // Tell screen to redraw at the next step
                    if (!redraw_flag) redraw_flag = 1;
                }
            }
        }
    }
}
