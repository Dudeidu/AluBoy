#include "emu_gpu.h"
#include "macros.h"

#include <stdio.h>

#define PIXELS_PER_BYTE 1
#define BITS_PER_PIXEL  8

u8* sprite_buffer      = NULL;
u8* background_buffer  = NULL;
u8* window_buffer      = NULL;

u8 redraw_flag;

int emu_gpu_init()
{
    // Allocate memory for the buffer and initialize with 0
    int buffer_size = (SCREEN_WIDTH * SCREEN_HEIGHT) / PIXELS_PER_BYTE;
    sprite_buffer = (u8*)calloc(buffer_size, sizeof(u8));
    if (sprite_buffer == NULL)
    {
        fprintf(stderr, "Failed to allocate memory for the pixel buffer!\n");
        return -1;
    }

    background_buffer = (u8*)calloc(255 * 255, sizeof(u8));
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

    // Debug
    /*
    for (int i = 0; i < buffer_size; i++)
    {
        sprite_buffer[i] = i % 4;
    }

    // Debug tile
    u8 tile_data[] = {
        0xFF, 0x00, 0x7E, 0xFF, 0x85, 0x81, 0x89, 0x83, 0x93, 0x85, 0xA5, 0x8B, 0xC9, 0x97, 0x7E, 0xFF
    };
    emu_gpu_draw_tile(10, 10, tile_data, 5);
    */
    return 0;
}

u8* emu_gpu_get_pixel_buffer()
{
    return background_buffer;
}

void emu_gpu_cleanup()
{
    if (sprite_buffer) free(sprite_buffer);
}

void emu_gpu_set_redraw_flag(u8 val)
{
    redraw_flag = val;
}

u8 emu_gpu_get_redraw_flag() {
    return redraw_flag;
}

void emu_gpu_set_pixel(int x, int y, u8 color_index)
{
    int pos = (y * SCREEN_WIDTH + x);
    
    // Set the pixel value in the pixel buffer
    background_buffer[pos] = color_index;
}

/// <summary>
/// Draws an 8x8 pixels tile to a buffer
/// </summary>
/// <param name="x">screen coordinate</param>
/// <param name="y">screen coordinate</param>
/// <param name="tile_data">pointer to the tile data</param>
void emu_gpu_draw_tile(int x, int y, u8* tile_data, BufferType buffer_type)
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
