#include "bookmark.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int bookmark_file_path(char *out, size_t size)
{
    const char *base = getenv("XDG_STATE_HOME");
    char directory[PATH_MAX];
    if (base && *base)
        snprintf(directory, sizeof(directory), "%s/cbzview", base);
    else {
        base = getenv("HOME");
        if (!base)
            return 0;
        snprintf(directory, sizeof(directory), "%s/.local/state/cbzview", base);
    }
    mkdir(directory, 0755);
    return snprintf(out, size, "%s/bookmarks", directory) < (int)size;
}

int bookmark_load(const char *filepath, int *page, LayoutMode *layout,
                  ReadDirection *direction, FitMode *fit)
{
    char real_path[PATH_MAX], file_path[PATH_MAX], line[PATH_MAX + 64], saved[PATH_MAX];
    if (!realpath(filepath, real_path) || !bookmark_file_path(file_path, sizeof(file_path)))
        return 0;

    FILE *file = fopen(file_path, "r");
    if (!file)
        return 0;

    int p, l, d, f, found = 0;
    while (fgets(line, sizeof(line), file) &&
        sscanf(line, "%1023[^:]:%d:%d:%d:%d", saved, &p, &l, &d, &f) == 5) {
        if (strcmp(saved, real_path) == 0) {
            *page = p;
            *layout = (LayoutMode)l;
            *direction = (ReadDirection)d;
            *fit = (FitMode)f;
            found = 1;
            break;
        }
        }
        fclose(file);
        return found;
}

void bookmark_save(const char *filepath, int page, LayoutMode layout,
                   ReadDirection direction, FitMode fit)
{
    char real_path[PATH_MAX], file_path[PATH_MAX];
    if (!realpath(filepath, real_path) || !bookmark_file_path(file_path, sizeof(file_path)))
        return;

    char lines[512][PATH_MAX + 64];
    int count = 0, replaced = 0;

    FILE *file = fopen(file_path, "r");
    if (file) {
        while (count < 512 && fgets(lines[count], sizeof(lines[0]), file)) {
            char saved[PATH_MAX];
            if (sscanf(lines[count], "%1023[^:]:", saved) == 1 && strcmp(saved, real_path) == 0) {
                snprintf(lines[count], sizeof(lines[0]), "%s:%d:%d:%d:%d\n",
                         real_path, page, layout, direction, fit);
                replaced = 1;
            }
            count++;
        }
        fclose(file);
    }

    if (!replaced && count < 512) {
        snprintf(lines[count++], sizeof(lines[0]), "%s:%d:%d:%d:%d\n",
                 real_path, page, layout, direction, fit);
    }

    file = fopen(file_path, "w");
    if (!file)
        return;
    for (int i = 0; i < count; i++)
        fputs(lines[i], file);
    fclose(file);
}
