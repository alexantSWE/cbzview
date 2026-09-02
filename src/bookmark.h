#ifndef BOOKMARK_H
#define BOOKMARK_H

#include "renderer.h"

int bookmark_load(const char *filepath, int *page, LayoutMode *layout,
                  ReadDirection *direction, FitMode *fit);
void bookmark_save(const char *filepath, int page, LayoutMode layout,
                   ReadDirection direction, FitMode fit);

#endif /* BOOKMARK_H */
