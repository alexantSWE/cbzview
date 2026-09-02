#include "osd.h"

#include <GL/gl.h>
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>

OSD *osd_init(void)
{
    return calloc(1, sizeof(OSD));
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

void osd_draw(OSD *osd, const CBZArchive *arch, int cur_page, int win_w, int win_h)
{
    if (!osd || glfwGetTime() > osd->visible_until)
        return;

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, win_w, win_h, 0, -1, 1);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    float bar_h = 32.0f;
    float pad = 24.0f;
    float track_y = win_h - bar_h / 2.0f;
    int total = (arch->total_pages > 1) ? arch->total_pages - 1 : 1;
    float progress = (float)cur_page / (float)total;
    float progress_x = pad + progress * (win_w - 2.0f * pad);

    /* Background bar */
    glColor4f(0.0f, 0.0f, 0.0f, 0.75f);
    glBegin(GL_QUADS);
    glVertex2f(0.0f, win_h - bar_h);
    glVertex2f((float)win_w, win_h - bar_h);
    glVertex2f((float)win_w, (float)win_h);
    glVertex2f(0.0f, (float)win_h);
    glEnd();

    /* Track line */
    glColor4f(0.25f, 0.25f, 0.25f, 1.0f);
    glLineWidth(4.0f);
    glBegin(GL_LINES);
    glVertex2f(pad, track_y);
    glVertex2f(win_w - pad, track_y);
    glEnd();

    /* Filled progress bar */
    glColor4f(0.85f, 0.2f, 0.2f, 1.0f);
    glBegin(GL_LINES);
    glVertex2f(pad, track_y);
    glVertex2f(progress_x, track_y);
    glEnd();

    /* Handle indicator */
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glPointSize(8.0f);
    glBegin(GL_POINTS);
    glVertex2f(progress_x, track_y);
    glEnd();

    glDisable(GL_BLEND);

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
}

void osd_cleanup(OSD *osd)
{
    free(osd);
}
