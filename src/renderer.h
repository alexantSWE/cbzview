#ifndef RENDERER_H
#define RENDERER_H

#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif

#include <GL/gl.h>
#include <GL/glext.h>

#include "decoder.h"

typedef enum {
    LAYOUT_SINGLE = 0,
    LAYOUT_DUAL,
    LAYOUT_WEBTOON
} LayoutMode;

typedef enum {
    DIR_LTR = 0,
    DIR_RTL
} ReadDirection;

typedef enum {
    FIT_HEIGHT = 0,
    FIT_WIDTH
} FitMode;

typedef struct {
    GLuint tex_id;
    GLuint pbo_id;
    int page_idx;
    int alloc_w;
    int alloc_h;
} GPUTextureSlot;

typedef struct {
    LayoutMode layout;
    ReadDirection direction;
    FitMode fit;
    int contrast_boost;
    float zoom;
    float pan_x;
    float pan_y;

    GLuint shader_prog;
    GLint u_tex;
    GLint u_tex_size;
    GLint u_contrast;

    GPUTextureSlot textures[CACHE_CAPACITY];
} ComicRenderer;

ComicRenderer *renderer_init(void);
void renderer_render_frame(ComicRenderer *rend, PageDecoder *dec, int cur_page, int win_w, int win_h);
void renderer_cleanup(ComicRenderer *rend);

#endif /* RENDERER_H */
