#ifndef ARCHIVE_H
#define ARCHIVE_H

#include "comicinfo.h"
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <zip.h>

typedef struct {
    char *filename;
    zip_uint64_t index;
    uint64_t uncomp_size;
    ComicPageType type;
} PageMeta;

typedef struct {
    zip_t *za;
    int fd;
    void *mmap_base;
    size_t file_size;
    PageMeta *pages;
    int total_pages;
    ComicInfo *info;
    pthread_mutex_t lock; /* Protects non-thread-safe libzip reads */
} CBZArchive;

CBZArchive *archive_open(const char *filepath);
unsigned char *archive_read_file(CBZArchive *arch, int page_idx, size_t *out_size);
void archive_close(CBZArchive *arch);

#endif /* ARCHIVE_H */
