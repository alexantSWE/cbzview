#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif

#include "osd.h"

#include <GLFW/glfw3.h>
#include <stdlib.h>

static GLuint make_osd_shader(void)
{
    static const char *vs_src =
        "#version 330 core\n"
        "layout (location = 0) in vec2 a_pos;\n"
        "uniform vec2 u_viewport;\n"
        "uniform vec4 u_rect;\n"
        "void main() {\n"
        "    vec2 p = mix(u_rect.xy, u_rect.zw, a_pos);\n"
        "    gl_Position = vec4((p.x / (u_viewport.x * 0.5)) - 1.0,\n"
        "                       1.0 - (p.y / (u_viewport.y * 0.5)), 0.0, 1.0);\n"
        "}\n";
    static const char *fs_src =
        "#version 330 core\n"
        "out vec4 FragColor;\n"
        "uniform vec4 u_color;\n"
        "void main() { FragColor = u_color; }\n";

    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vs_src, NULL);
    glCompileShader(vs);
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fs_src, NULL);
    glCompileShader(fs);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

OSD *osd_init(void)
{
    OSD *osd = calloc(1, sizeof(*osd));
    if (!osd)
        return NULL;
    osd->shader_prog = make_osd_shader();
    if (osd->shader_prog) {
        osd->u_viewport = glGetUniformLocation(osd->shader_prog, "u_viewport");
        osd->u_rect = glGetUniformLocation(osd->shader_prog, "u_rect");
        osd->u_color = glGetUniformLocation(osd->shader_prog, "u_color");
    }
    static const float quad[] = {
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 1.0f, 1.0f, 1.0f
    };
    glGenVertexArrays(1, &osd->vao);
    glGenBuffers(1, &osd->vbo);
    glBindVertexArray(osd->vao);
    glBindBuffer(GL_ARRAY_BUFFER, osd->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void *)0);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return osd;
}

void osd_trigger(OSD *osd)
{
    if (osd)
        osd->visible_until = glfwGetTime() + 2.5;
}

int osd_is_active(OSD *osd)
{
    return osd && (glfwGetTime() < osd->visible_until);
}

static void draw_box(OSD *osd, float x0, float y0, float x1, float y1,
                     float r, float g, float b, float a)
{
    glUniform4f(osd->u_rect, x0, y0, x1, y1);
    glUniform4f(osd->u_color, r, g, b, a);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void osd_draw(OSD *osd, const CBZArchive *arch, int cur_page, int win_w, int win_h)
{
    if (!osd || glfwGetTime() > osd->visible_until || !osd->shader_prog)
        return;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(osd->shader_prog);
    glUniform2f(osd->u_viewport, (float)win_w, (float)win_h);
    glBindVertexArray(osd->vao);

    float bar_h = 32.0f;
    float pad = 24.0f;
    float track_y = (float)win_h - bar_h / 2.0f;
    int total = (arch->total_pages > 1) ? arch->total_pages - 1 : 1;
    float progress = (float)cur_page / (float)total;
    float progress_x = pad + progress * ((float)win_w - 2.0f * pad);
    draw_box(osd, 0.0f, (float)win_h - bar_h, (float)win_w, (float)win_h,
             0.0f, 0.0f, 0.0f, 0.75f);
    draw_box(osd, pad, track_y - 2.0f, (float)win_w - pad, track_y + 2.0f,
             0.25f, 0.25f, 0.25f, 1.0f);
    draw_box(osd, pad, track_y - 2.0f, progress_x, track_y + 2.0f,
             0.85f, 0.2f, 0.2f, 1.0f);
    draw_box(osd, progress_x - 5.0f, track_y - 5.0f,
             progress_x + 5.0f, track_y + 5.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    glBindVertexArray(0);
    glUseProgram(0);
    glDisable(GL_BLEND);
}

void osd_cleanup(OSD *osd)
{
    if (!osd)
        return;
    if (osd->shader_prog)
        glDeleteProgram(osd->shader_prog);
    if (osd->vao)
        glDeleteVertexArrays(1, &osd->vao);
    if (osd->vbo)
        glDeleteBuffers(1, &osd->vbo);
    free(osd);
}
