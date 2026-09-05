#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif

#include "renderer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static GLuint make_shader(GLenum type, const char *src)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);
    GLint ok;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "Shader compile error: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint make_bicubic_program(void)
{
    static const char *vs_src =
    "#version 330 core\n"
    "layout (location = 0) in vec2 a_pos;\n"
    "uniform vec2 u_viewport;\n"
    "uniform vec2 u_pan;\n"
    "uniform float u_zoom;\n"
    "uniform vec4 u_rect;\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "    v_uv = a_pos;\n"
    "    vec2 p = mix(u_rect.xy, u_rect.zw, a_pos);\n"
    "    vec2 world = p * u_zoom + u_pan;\n"
    "    gl_Position = vec4(world.x / (u_viewport.x * 0.5), -world.y / (u_viewport.y * 0.5), 0.0, 1.0);\n"
    "}\n";

    static const char *fs_src =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "out vec4 FragColor;\n"
    "uniform sampler2D u_tex;\n"
    "uniform vec2 u_tex_size;\n"
    "uniform int u_contrast;\n"
    "uniform int u_is_placeholder;\n"
    "\n"
    "vec4 sampleBicubic(sampler2D tex, vec2 uv, vec2 texSize) {\n"
    "    vec2 pt = uv * texSize - 0.5;\n"
    "    vec2 f = fract(pt);\n"
    "    vec2 i = floor(pt);\n"
    "    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));\n"
    "    vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);\n"
    "    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));\n"
    "    vec2 w3 = f * f * (-0.5 + 0.5 * f);\n"
    "    vec2 w12 = w1 + w2;\n"
    "    vec2 offset12 = w2 / (w1 + w2);\n"
    "    vec2 texPos0 = (i - 0.5) / texSize;\n"
    "    vec2 texPos12 = (i + offset12) / texSize;\n"
    "    vec2 texPos3 = (i + 2.5) / texSize;\n"
    "    vec4 res = vec4(0.0);\n"
    "    res += texture(tex, vec2(texPos0.x,  texPos0.y))  * w0.x  * w0.y;\n"
    "    res += texture(tex, vec2(texPos12.x, texPos0.y))  * w12.x * w0.y;\n"
    "    res += texture(tex, vec2(texPos3.x,  texPos0.y))  * w3.x  * w0.y;\n"
    "    res += texture(tex, vec2(texPos0.x,  texPos12.y)) * w0.x  * w12.y;\n"
    "    res += texture(tex, vec2(texPos12.x, texPos12.y)) * w12.x * w12.y;\n"
    "    res += texture(tex, vec2(texPos3.x,  texPos12.y)) * w3.x  * w12.y;\n"
    "    res += texture(tex, vec2(texPos0.x,  texPos3.y))  * w0.x  * w3.y;\n"
    "    res += texture(tex, vec2(texPos12.x, texPos3.y))  * w12.x * w3.y;\n"
    "    res += texture(tex, vec2(texPos3.x,  texPos3.y))  * w3.x  * w3.y;\n"
    "    return clamp(res, 0.0, 1.0);\n"
    "}\n"
    "\n"
    "void main() {\n"
    "    if (u_is_placeholder == 1) {\n"
    "        vec2 grid = step(vec2(0.06), fract(v_uv * 18.0));\n"
    "        float pat = mix(0.12, 0.17, grid.x * grid.y);\n"
    "        FragColor = vec4(vec3(pat), 1.0);\n"
    "        return;\n"
    "    }\n"
    "    vec4 c = sampleBicubic(u_tex, v_uv, u_tex_size);\n"
    "    if (u_contrast == 1) {\n"
    "        c.rgb = clamp((c.rgb - 0.08) / 0.84, 0.0, 1.0);\n"
    "    }\n"
    "    FragColor = c;\n"
    "}\n";

    GLuint vs = make_shader(GL_VERTEX_SHADER, vs_src);
    GLuint fs = make_shader(GL_FRAGMENT_SHADER, fs_src);
    if (!vs || !fs)
        return 0;

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

ComicRenderer *renderer_init(void)
{
    ComicRenderer *rend = calloc(1, sizeof(*rend));
    if (!rend)
        return NULL;

    rend->layout = LAYOUT_SINGLE;
    rend->direction = DIR_LTR;
    rend->fit = FIT_HEIGHT;
    rend->zoom = 1.0f;
    rend->spread_offset = 0;

    /* Tight byte alignment prevents invalid row stride calculations on RGB images */
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

    rend->shader_prog = make_bicubic_program();
    if (rend->shader_prog) {
        rend->u_tex = glGetUniformLocation(rend->shader_prog, "u_tex");
        rend->u_tex_size = glGetUniformLocation(rend->shader_prog, "u_tex_size");
        rend->u_contrast = glGetUniformLocation(rend->shader_prog, "u_contrast");
        rend->u_viewport = glGetUniformLocation(rend->shader_prog, "u_viewport");
        rend->u_pan = glGetUniformLocation(rend->shader_prog, "u_pan");
        rend->u_zoom = glGetUniformLocation(rend->shader_prog, "u_zoom");
        rend->u_rect = glGetUniformLocation(rend->shader_prog, "u_rect");
        rend->u_is_placeholder = glGetUniformLocation(rend->shader_prog, "u_is_placeholder");
    }

    static const float quad_verts[] = {
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 1.0f, 1.0f, 1.0f
    };
    glGenVertexArrays(1, &rend->vao);
    glGenBuffers(1, &rend->vbo);
    glBindVertexArray(rend->vao);
    glBindBuffer(GL_ARRAY_BUFFER, rend->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad_verts), quad_verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void *)0);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    for (int i = 0; i < CACHE_CAPACITY; i++) {
        glGenTextures(1, &rend->textures[i].tex_id);
        glBindTexture(GL_TEXTURE_2D, rend->textures[i].tex_id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        rend->textures[i].page_idx = -1;

        glGenBuffers(1, &rend->textures[i].pbo_id);
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    return rend;
}

static GLuint sync_texture(ComicRenderer *rend, DecodedSlot *slot, int slot_idx)
{
    if (!slot || !slot->is_ready || !slot->rgba || slot->width <= 0 || slot->height <= 0 ||
        slot_idx < 0 || slot_idx >= CACHE_CAPACITY)
        return 0;

    GPUTextureSlot *tex_slot = &rend->textures[slot_idx];
    GLenum gl_fmt = (slot->channels == 3) ? GL_RGB : GL_RGBA;
    GLenum gl_internal = (slot->channels == 3) ? GL_RGB8 : GL_RGBA8;
    size_t bpp = (size_t)slot->channels;

    if (tex_slot->page_idx != slot->page_idx) {
        size_t size = (size_t)slot->width * (size_t)slot->height * bpp;

        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

        /* Fast streaming DMA transfer into PBO */
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, tex_slot->pbo_id);
        glBufferData(GL_PIXEL_UNPACK_BUFFER, (GLsizeiptr)size, slot->rgba, GL_STREAM_DRAW);

        glBindTexture(GL_TEXTURE_2D, tex_slot->tex_id);
        if (tex_slot->alloc_w == slot->width && tex_slot->alloc_h == slot->height &&
            tex_slot->alloc_channels == slot->channels) {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, slot->width, slot->height,
                            gl_fmt, GL_UNSIGNED_BYTE, 0);
            } else {
                glTexImage2D(GL_TEXTURE_2D, 0, (GLint)gl_internal, slot->width, slot->height,
                             0, gl_fmt, GL_UNSIGNED_BYTE, 0);
                tex_slot->alloc_w = slot->width;
                tex_slot->alloc_h = slot->height;
                tex_slot->alloc_channels = slot->channels;
            }

            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
            tex_slot->page_idx = slot->page_idx;
    }

    return tex_slot->tex_id;
}

static void draw_quad(ComicRenderer *rend, GLuint tex_id, int width, int height,
                      float x0, float y0, float x1, float y1)
{
    if (!rend->shader_prog)
        return;

    glUniform4f(rend->u_rect, x0, y0, x1, y1);
    if (tex_id && width > 0 && height > 0) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex_id);
        glUniform1i(rend->u_tex, 0);
        glUniform2f(rend->u_tex_size, (float)width, (float)height);
        glUniform1i(rend->u_is_placeholder, 0);
    } else {
        glUniform1i(rend->u_is_placeholder, 1);
    }

    glBindVertexArray(rend->vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
}

void renderer_render_frame(ComicRenderer *rend, PageDecoder *dec, int cur_page, int win_w, int win_h)
{
    glViewport(0, 0, win_w, win_h);
    glClearColor(0.06f, 0.06f, 0.06f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (rend->shader_prog) {
        glUseProgram(rend->shader_prog);
        glUniform1i(rend->u_contrast, rend->contrast_boost);
        glUniform2f(rend->u_viewport, (float)win_w, (float)win_h);
        glUniform2f(rend->u_pan, rend->pan_x, rend->pan_y);
        glUniform1f(rend->u_zoom, rend->zoom);
    }

    decoder_lock(dec);

    int cur_sidx = decoder_get_slot_index_locked(dec, cur_page);
    DecodedSlot *cur_slot = (cur_sidx >= 0) ? &dec->slots[cur_sidx] : NULL;

    int is_cover_or_spread = 0;
    if (dec->arch->info && dec->arch->info->has_metadata && cur_page < dec->arch->info->page_type_count) {
        ComicPageType pt = dec->arch->info->page_types[cur_page];
        if (pt == PAGE_TYPE_FRONT_COVER || pt == PAGE_TYPE_SPREAD)
            is_cover_or_spread = 1;
    }

    int is_wide = cur_slot && cur_slot->is_ready && cur_slot->width > 0 && cur_slot->height > 0 &&
                  (cur_slot->width > cur_slot->height * 1.15f);

    if (rend->layout == LAYOUT_WEBTOON) {
        if (cur_slot && cur_slot->is_ready && cur_slot->width > 0 && cur_slot->height > 0) {
            float scale = (float)win_w / (float)cur_slot->width;
            float h = cur_slot->height * scale;
            float top = -h / 2.0f;
            int cur_w = cur_slot->width;
            int cur_h = cur_slot->height;
            GLuint cur_tex = sync_texture(rend, cur_slot, cur_sidx);

            GLuint next_tex = 0;
            int next_w = 0, next_h_px = 0;
            float nh = 0.0f;
            if (cur_page + 1 < dec->arch->total_pages) {
                int next_sidx = decoder_get_slot_index_locked(dec, cur_page + 1);
                if (next_sidx >= 0) {
                    DecodedSlot *next = &dec->slots[next_sidx];
                    next_w = next->width;
                    next_h_px = next->height;
                    nh = next->height * ((float)win_w / (float)next->width);
                    next_tex = sync_texture(rend, next, next_sidx);
                }
            }

            GLuint prev_tex = 0;
            int prev_w = 0, prev_h_px = 0;
            float ph = 0.0f;
            if (cur_page > 0) {
                int prev_sidx = decoder_get_slot_index_locked(dec, cur_page - 1);
                if (prev_sidx >= 0) {
                    DecodedSlot *prev = &dec->slots[prev_sidx];
                    prev_w = prev->width;
                    prev_h_px = prev->height;
                    ph = prev->height * ((float)win_w / (float)prev->width);
                    prev_tex = sync_texture(rend, prev, prev_sidx);
                }
            }

            decoder_unlock(dec);

            draw_quad(rend, cur_tex, cur_w, cur_h, -(float)win_w / 2.0f, top, (float)win_w / 2.0f, top + h);
            if (next_tex)
                draw_quad(rend, next_tex, next_w, next_h_px, -(float)win_w / 2.0f, top + h, (float)win_w / 2.0f, top + h + nh);
            if (prev_tex)
                draw_quad(rend, prev_tex, prev_w, prev_h_px, -(float)win_w / 2.0f, top - ph, (float)win_w / 2.0f, top);

            goto finish_render;
        }
    } else if (rend->layout == LAYOUT_SINGLE || (cur_page == 0 && !rend->spread_offset) || is_wide || is_cover_or_spread) {
        if (cur_slot && cur_slot->is_ready) {
            float scale = (rend->fit == FIT_HEIGHT) ? ((float)win_h / (float)cur_slot->height)
            : ((float)win_w / (float)cur_slot->width);
            float w = cur_slot->width * scale;
            float h = cur_slot->height * scale;
            int slot_w = cur_slot->width;
            int slot_h = cur_slot->height;
            GLuint tex = sync_texture(rend, cur_slot, cur_sidx);

            decoder_unlock(dec);
            draw_quad(rend, tex, slot_w, slot_h, -w / 2.0f, -h / 2.0f, w / 2.0f, h / 2.0f);
            goto finish_render;
        } else {
            decoder_unlock(dec);
            float h = (float)win_h * 0.9f;
            float w = h * 0.7f;
            draw_quad(rend, 0, 0, 0, -w / 2.0f, -h / 2.0f, w / 2.0f, h / 2.0f);
            goto finish_render;
        }
    } else {
        int p0 = cur_page;
        int p1 = cur_page + 1;
        if (p1 >= dec->arch->total_pages)
            p1 = -1;

        int left_idx = (rend->direction == DIR_LTR) ? p0 : p1;
        int right_idx = (rend->direction == DIR_LTR) ? p1 : p0;

        int left_sidx = (left_idx >= 0) ? decoder_get_slot_index_locked(dec, left_idx) : -1;
        int right_sidx = (right_idx >= 0) ? decoder_get_slot_index_locked(dec, right_idx) : -1;

        DecodedSlot *left_slot = (left_sidx >= 0) ? &dec->slots[left_sidx] : NULL;
        DecodedSlot *right_slot = (right_sidx >= 0) ? &dec->slots[right_sidx] : NULL;
        int left_ready = left_slot && left_slot->is_ready && left_slot->width > 0 && left_slot->height > 0;
        int right_ready = right_slot && right_slot->is_ready && right_slot->width > 0 && right_slot->height > 0;

        if (left_ready && right_ready) {
            float total_w = (float)(left_slot->width + right_slot->width);
            int max_h = (left_slot->height > right_slot->height) ? left_slot->height : right_slot->height;
            float scale = (rend->fit == FIT_HEIGHT) ? ((float)win_h / (float)max_h)
            : ((float)win_w / total_w);

            float lw = left_slot->width * scale;
            float lh = left_slot->height * scale;
            float rw = right_slot->width * scale;
            float rh = right_slot->height * scale;

            int lw_px = left_slot->width;
            int lh_px = left_slot->height;
            int rw_px = right_slot->width;
            int rh_px = right_slot->height;

            GLuint left_tex = sync_texture(rend, left_slot, left_sidx);
            GLuint right_tex = sync_texture(rend, right_slot, right_sidx);

            decoder_unlock(dec);
            draw_quad(rend, left_tex, lw_px, lh_px, -lw, -lh / 2.0f, 0.0f, lh / 2.0f);
            draw_quad(rend, right_tex, rw_px, rh_px, 0.0f, -rh / 2.0f, rw, rh / 2.0f);
            goto finish_render;
        } else if (left_ready || right_ready) {
            DecodedSlot *single = left_ready ? left_slot : right_slot;
            int single_sidx = left_ready ? left_sidx : right_sidx;
            float scale = (rend->fit == FIT_HEIGHT) ? ((float)win_h / (float)single->height)
            : ((float)win_w / (float)single->width);
            float w = single->width * scale;
            float h = single->height * scale;
            int single_w = single->width;
            int single_h = single->height;
            GLuint single_tex = sync_texture(rend, single, single_sidx);

            decoder_unlock(dec);
            if (left_ready) {
                draw_quad(rend, single_tex, single_w, single_h, -w, -h / 2.0f, 0.0f, h / 2.0f);
                draw_quad(rend, 0, 0, 0, 0.0f, -h / 2.0f, w, h / 2.0f);
            } else {
                draw_quad(rend, 0, 0, 0, -w, -h / 2.0f, 0.0f, h / 2.0f);
                draw_quad(rend, single_tex, single_w, single_h, 0.0f, -h / 2.0f, w, h / 2.0f);
            }
            goto finish_render;
        } else {
            decoder_unlock(dec);
            float h = (float)win_h * 0.9f;
            float w = h * 0.7f;
            draw_quad(rend, 0, 0, 0, -w, -h / 2.0f, 0.0f, h / 2.0f);
            draw_quad(rend, 0, 0, 0, 0.0f, -h / 2.0f, w, h / 2.0f);
            goto finish_render;
        }
    }

    decoder_unlock(dec);

    finish_render:
    if (rend->shader_prog)
        glUseProgram(0);
}

void renderer_cleanup(ComicRenderer *rend)
{
    if (!rend)
        return;
    if (rend->shader_prog)
        glDeleteProgram(rend->shader_prog);
    if (rend->vao)
        glDeleteVertexArrays(1, &rend->vao);
    if (rend->vbo)
        glDeleteBuffers(1, &rend->vbo);
    for (int i = 0; i < CACHE_CAPACITY; i++) {
        glDeleteTextures(1, &rend->textures[i].tex_id);
        glDeleteBuffers(1, &rend->textures[i].pbo_id);
    }
    free(rend);
}
