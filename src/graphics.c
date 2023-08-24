#include "graphics.h"

#include "macros.h"


SDL_GLContext   m_context;
GLuint          m_vao, m_vbo, m_ebo, m_tex;
GLuint          m_vert_shader;
GLuint          m_frag_shader;
GLuint          m_shader_prog;

const char* vert_shader_src = "\
#version 150 core                                                            \n\
in vec2 in_Position;                                                         \n\
in vec2 in_Texcoord;                                                         \n\
out vec2 Texcoord;                                                           \n\
void main()                                                                  \n\
{                                                                            \n\
    Texcoord = in_Texcoord;                                                  \n\
    gl_Position = vec4(in_Position, 0.0, 1.0);                               \n\
}                                                                            \n\
";

const char* frag_shader_src = "\
#version 150 core                                                            \n\
in vec2 Texcoord;                                                            \n\
out vec4 out_Color;                                                          \n\
uniform sampler2D tex;                                                       \n\
void main()                                                                  \n\
{                                                                            \n\
    out_Color = texture(tex, Texcoord);                                      \n\
}                                                                            \n\
";

const GLfloat verts[6][4] = {
    //  x      y      s      t
    { -1.0f, -1.0f,  0.0f,  1.0f }, // BL
    { -1.0f,  1.0f,  0.0f,  0.0f }, // TL
    {  1.0f,  1.0f,  1.0f,  0.0f }, // TR
    {  1.0f, -1.0f,  1.0f,  1.0f }, // BR
};

// The order of verts to draw (makes a quad)
const GLint indicies[] = {
    0, 1, 2, 0, 2, 3
};

const SDL_Color palette[] = {
    {245, 250, 239, 255},   // white (greenish)
    {134, 194, 112, 255},   // light
    {47, 105, 87, 255},     // dark
    {0, 0, 0, 255},         // black

    // ... add more colors to your palette
};

uint8_t* rgba_buffer = NULL;


int graphics_init(SDL_Window* window)
{
    GLenum err;

    // Initialize rendering context
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(
        SDL_GL_CONTEXT_PROFILE_MASK,
        SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    m_context = SDL_GL_CreateContext(window);
    if (m_context == NULL) {
        fprintf(stderr, "Failed to create GL context\n");
        return 0;
    }

    //SDL_GL_SetSwapInterval(1); // Use VSYNC

    // Initialize GL Extension Wrangler (GLEW)
    glewExperimental = GL_TRUE; // Please expose OpenGL 3.x+ interfaces
    err = glewInit();
    if (err != GLEW_OK) {
        fprintf(stderr, "Failed to init GLEW\n");
        SDL_GL_DeleteContext(m_context);
        return 0;
    }

    // Initialize the color buffer
    if (create_rgba_buffer() == -1)
    {
        return 0;
    }

    // Return 0 if failed to initialize
    if (!init_shaders() || !init_geometry() || !init_textures())
    {
        return 0;
    }

    return 1;
}

int init_shaders()
{
    GLint   status;
    char    err_buf[512];

    GL_CALL( glGenVertexArrays(1, &m_vao) );
    GL_CALL( glBindVertexArray(m_vao) );

    // Compile vertex shader
    m_vert_shader = glCreateShader(GL_VERTEX_SHADER);
    GL_CALL( glShaderSource(m_vert_shader, 1, &vert_shader_src, NULL) );
    GL_CALL( glCompileShader(m_vert_shader) );
    GL_CALL( glGetShaderiv(m_vert_shader, GL_COMPILE_STATUS, &status) );
    if (status != GL_TRUE) {
        GL_CALL(glGetShaderInfoLog(m_vert_shader, sizeof(err_buf), NULL, err_buf));
        err_buf[sizeof(err_buf) - 1] = '\0';
        fprintf(stderr, "Vertex shader compilation failed: %s\n", err_buf);
        return 0;
    }

    // Compile fragment shader
    m_frag_shader = glCreateShader(GL_FRAGMENT_SHADER);
    GL_CALL( glShaderSource(m_frag_shader, 1, &frag_shader_src, NULL) );
    GL_CALL( glCompileShader(m_frag_shader) );
    GL_CALL( glGetShaderiv(m_frag_shader, GL_COMPILE_STATUS, &status) );
    if (status != GL_TRUE) {
        GL_CALL(glGetShaderInfoLog(m_frag_shader, sizeof(err_buf), NULL, err_buf) );
        err_buf[sizeof(err_buf) - 1] = '\0';
        fprintf(stderr, "Fragment shader compilation failed: %s\n", err_buf);
        return 0;
    }

    // Link vertex and fragment shaders
    m_shader_prog = glCreateProgram();
    GL_CALL( glAttachShader(m_shader_prog, m_vert_shader) );
    GL_CALL( glAttachShader(m_shader_prog, m_frag_shader) );
    GL_CALL( glBindFragDataLocation(m_shader_prog, 0, "out_Color") );
    GL_CALL( glLinkProgram(m_shader_prog) );
    GL_CALL( glUseProgram(m_shader_prog) );

    return 1;
}

int init_geometry()
{
    GLint pos_attr_loc, tex_attr_loc;

    // Populate vertex buffer
    GL_CALL( glGenBuffers(1, &m_vbo) );
    GL_CALL( glBindBuffer(GL_ARRAY_BUFFER, m_vbo) );
    GL_CALL( glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW) );

    // Populate element buffer
    GL_CALL( glGenBuffers(1, &m_ebo));
    GL_CALL( glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo));
    GL_CALL( glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indicies), indicies, GL_STATIC_DRAW));

    // Bind vertex position attribute
    pos_attr_loc = glGetAttribLocation(m_shader_prog, "in_Position");
    GL_CALL( glVertexAttribPointer(pos_attr_loc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void*)0) );
    GL_CALL( glEnableVertexAttribArray(pos_attr_loc) );

    // Bind vertex texture coordinate attribute
    tex_attr_loc = glGetAttribLocation(m_shader_prog, "in_Texcoord");
    GL_CALL( glVertexAttribPointer(tex_attr_loc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void*)(2 * sizeof(GLfloat))) );
    GL_CALL( glEnableVertexAttribArray(tex_attr_loc) );

    return 1;
}

int init_textures()
{
    GL_CALL( glGenTextures(1, &m_tex) );
    GL_CALL( glActiveTexture(GL_TEXTURE0) );
    GL_CALL( glBindTexture(GL_TEXTURE_2D, m_tex) );
    GL_CALL( glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SCREEN_WIDTH, SCREEN_HEIGHT, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL) );
    GL_CALL( glUniform1i(glGetUniformLocation(m_shader_prog, "tex"), 0) );
    GL_CALL( glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER) );
    GL_CALL( glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER) );
    GL_CALL( glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST) );
    GL_CALL( glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST) );
    GL_CALL(glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, rgba_buffer) );
    GL_CALL( glEnable(GL_BLEND) );
    GL_CALL( glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA) );
    
    return 1;
}

void graphics_cleanup()
{

    GL_CALL( glUseProgram(0) );
    GL_CALL( glDisableVertexAttribArray(0) );
    GL_CALL( glDetachShader(m_shader_prog, m_vert_shader) );
    GL_CALL( glDetachShader(m_shader_prog, m_frag_shader) );
    GL_CALL( glDeleteProgram(m_shader_prog) );
    GL_CALL( glDeleteShader(m_vert_shader) );
    GL_CALL( glDeleteShader(m_frag_shader) );
    GL_CALL( glDeleteTextures(1, &m_tex) );
    GL_CALL( glDeleteBuffers(1, &m_ebo) );
    GL_CALL( glDeleteBuffers(1, &m_vbo) );
    GL_CALL( glDeleteVertexArrays(1, &m_vao) );
    SDL_GL_DeleteContext(m_context);

    if (rgba_buffer) free(rgba_buffer);
}

int create_rgba_buffer()
{
    // The actual colors (each pixel gets 4 bytes: r,g,b,a)
    rgba_buffer = (u8*)calloc(SCREEN_WIDTH * SCREEN_HEIGHT * 4, sizeof(u8));
    if (rgba_buffer == NULL)
    {
        fprintf(stderr, "Failed to allocate memory for the rgba buffer!\n");
        return -1;
    }
    return 0;
}

/*
int8_t* graphics_get_pixel_buffer()
{
    return rgba_buffer;
}
*/

void graphics_update_rgba_buffer(u8* color_index_buffer)
{
    // Iterate through a buffer of color_index and update the rgba buffer
    // with the corresponding colors from the palette
    int16_t     buffer_size = SCREEN_WIDTH * SCREEN_HEIGHT;
    SDL_Color   color;
    int         pos;

    for (int i = 0; i < buffer_size; i++) {
        pos = i * 4;
        color = palette[color_index_buffer[i]];

        rgba_buffer[pos]     = color.r;
        rgba_buffer[pos + 1] = color.g;
        rgba_buffer[pos + 2] = color.b;
        rgba_buffer[pos + 3] = color.a;
    }

    /*
    // print out the buffer contents
    buffer_size = 160 * 144 * 4;
    char* str;
    for (int i = 0; i < buffer_size; i++) {
        printf("%d ", rgba_buffer[i]);
    }
    */

    // Update the texture with the new color data
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, rgba_buffer);
}

void graphics_draw(SDL_Window* window)
{
    // Clears the screen buffer to red
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Draw stuff here
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, NULL);

    // Sends the render to SDL
    SDL_GL_SwapWindow(window);
    return 0;
}