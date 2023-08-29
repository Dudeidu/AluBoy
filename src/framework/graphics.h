#ifndef GRAPHICS_H
#define GRAPHICS_H

// Abstractions for SDL_GL and OpenGL stuff
#include <stdio.h>
#include <SDL.h>
#include <GL/glew.h>

#include "alu_binary.h"

// Wrapper macro for gl_ functions, prints out the errors
#define GL_CALL(func) \
do { \
    func; \
    GLenum err = glGetError(); \
    if (err != GL_NO_ERROR) { \
        fprintf(stderr, "OpenGL error after %s: %s\n", #func, gluErrorString(err)); \
    } \
} while (0)

int graphics_init(SDL_Window* window);
void graphics_cleanup();
void graphics_draw(SDL_Window* window);

void graphics_update_rgba_buffer(u8 * color_index_buffer);

#endif GRAPHICS_H

