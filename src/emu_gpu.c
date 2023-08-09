#include "emu_gpu.h"
#include "macros.h"

#include <stdbool.h>
#include <stdio.h>

#define PIXELS_PER_BYTE 1
#define BITS_PER_PIXEL  8

uint8_t* pixel_buffer = NULL;

int emu_gpu_create_pixel_buffer()
{
    // Allocate memory for the buffer and initialize with 0
    int buffer_size = (SCREEN_WIDTH * SCREEN_HEIGHT) / PIXELS_PER_BYTE;
    pixel_buffer = (uint8_t*)calloc(buffer_size, sizeof(uint8_t));
    if (pixel_buffer == NULL)
    {
        printf("Failed to allocate memory for the pixel buffer!\n");
        return -1;
    }

    // Debug
    for (int i = 0; i < buffer_size; i++)
    {
        pixel_buffer[i] = i % 4;
    }

    //gpu_set_pixel(5, 20, 3);
    //gpu_set_pixel(5, 10, 3);
    //gpu_set_pixel(13, 0, 3);
    //gpu_set_pixel(100, 50, 2);
    return 0;
}

int8_t* emu_gpu_get_pixel_buffer()
{
    return pixel_buffer;
}

void emu_gpu_cleanup()
{
    if (pixel_buffer) free(pixel_buffer);
}

void emu_gpu_set_pixel(int x, int y, uint8_t color_index)
{
    /*
    uint16_t    pos             = (y * SCREEN_WIDTH + x);
    uint8_t*    p_pixel_byte    = pixel_buffer + (pos / PIXELS_PER_BYTE); // Get the byte of the pixel in pixel_buffer
    uint8_t     bit_offset      = (pos % PIXELS_PER_BYTE) * BITS_PER_PIXEL;
    
    printf("(%d, %d) pos: %d, byte: %d, bit: %d\n", x, y, pos, (pos / PIXELS_PER_BYTE), bit_offset);
    
    //Set 2 bits in the byte corresponding to (x, y) of the pixel buffer to color_index. 
    //Each byte contains information for 2 pixels, so some bit manipulation is done.

    //The SDL surface has no 2BPP mode, the next smallest mode is 4BPP (4 bits per pixel).
    //Since the gameboy supports 4 colors, a pixel will only use the first 2 bits for color info.
    
    // Masks 2 bits (0b11) starting from bit position
    uint8_t mask = 0x3 << bit_offset;
    // Clears the masked bits, then set the bits to color_index
    *p_pixel_byte = (*p_pixel_byte & ~mask) | (color_index << bit_offset);

    printf("%d\n", *p_pixel_byte);
    */
    int pos = (y * SCREEN_WIDTH + x);
    
    // Set the pixel value in the pixel buffer
    pixel_buffer[pos] = color_index;



    /*
    // print out the buffer
    uint16_t buffer_size = 160 * 144;
    char* str;
    for (int i = 0; i < buffer_size; i++) {
        printf("%d ", pixel_buffer[i]);
        if ((i + 1) % SCREEN_WIDTH == 0) {
            printf("\n"); // Print a newline after each row
        }
    }
    */
}
