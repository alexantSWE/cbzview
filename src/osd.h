#ifndef OSD_H
#define OSD_H

#include "archive.h"

typedef struct {
    double visible_until;
} OSD;

OSD *osd_init(void);
void osd_trigger(OSD *osd);
int osd_is_active(OSD *osd);
void osd_draw(OSD *osd, const CBZArchive *arch, int cur_page, int win_w, int win_h);
void osd_cleanup(OSD *osd);

#endif /* OSD_H */
