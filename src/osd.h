#ifndef OSD_H
#define OSD_H

#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif

#include <GL/gl.h>
#include "archive.h"

typedef struct {
    double visible_until;
    GLuint shader_prog;
    GLuint vao;
    GLuint vbo;
    GLint u_viewport;
    GLint u_rect;
    GLint u_color;
} OSD;

OSD *osd_init(void);
void osd_trigger(OSD *osd);
int osd_is_active(OSD *osd);
void osd_draw(OSD *osd, const CBZArchive *arch, int cur_page, int win_w, int win_h);
void osd_cleanup(OSD *osd);

#endif /* OSD_H */
