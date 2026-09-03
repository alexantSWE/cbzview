#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "archive.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static int is_image_extension(const char *name)
{
    const char *ext = strrchr(name, '.');
    return ext && (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0 ||
    strcasecmp(ext, ".jfif") == 0 || strcasecmp(ext, ".webp") == 0 ||
    strcasecmp(ext, ".png") == 0 || strcasecmp(ext, ".avif") == 0 ||
    strcasecmp(ext, ".jxl") == 0);
}

static int compare_natural(const void *a, const void *b)
{
    const PageMeta *pa = a;
    const PageMeta *pb = b;
    return strverscmp(pa->filename, pb->filename);
}

CBZArchive *archive_open(const char *filepath)
{
    int fd = open(filepath, O_RDONLY);
    if (fd < 0) {
        perror("Error opening CBZ archive");
        return NULL;
    }

    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size <= 0) {
        close(fd);
        return NULL;
    }
    size_t file_size = (size_t)st.st_size;

    void *mmap_base = mmap(NULL, file_size, PROT_READ, MAP_SHARED, fd, 0);
    if (mmap_base == MAP_FAILED) {
        perror("mmap failed for CBZ file");
        close(fd);
        return NULL;
    }

    /* MADV_RANDOM prevents kernel thrashing on multi-gigabyte files */
    madvise(mmap_base, file_size, MADV_RANDOM);

    zip_error_t zerr;
    zip_error_init(&zerr);
    zip_source_t *src = zip_source_buffer_create(mmap_base, file_size, 0, &zerr);
    if (!src) {
        fprintf(stderr, "zip_source_buffer_create failed: %s\n", zip_error_strerror(&zerr));
        zip_error_fini(&zerr);
        munmap(mmap_base, file_size);
        close(fd);
        return NULL;
    }

    zip_t *za = zip_open_from_source(src, ZIP_RDONLY, &zerr);
    if (!za) {
        fprintf(stderr, "zip_open_from_source failed: %s\n", zip_error_strerror(&zerr));
        zip_source_free(src);
        zip_error_fini(&zerr);
        munmap(mmap_base, file_size);
        close(fd);
        return NULL;
    }
    zip_error_fini(&zerr);

    zip_int64_t num_entries = zip_get_num_entries(za, 0);
    if (num_entries <= 0) {
        zip_close(za);
        munmap(mmap_base, file_size);
        close(fd);
        return NULL;
    }

    CBZArchive *arch = calloc(1, sizeof(*arch));
    if (!arch) {
        zip_close(za);
        munmap(mmap_base, file_size);
        close(fd);
        return NULL;
    }

    arch->za = za;
    arch->fd = fd;
    arch->mmap_base = mmap_base;
    arch->file_size = file_size;
    pthread_mutex_init(&arch->lock, NULL);

    arch->pages = calloc((size_t)num_entries, sizeof(*arch->pages));
    if (!arch->pages) {
        archive_close(arch);
        return NULL;
    }

    zip_int64_t comic_info_idx = -1;

    for (zip_uint64_t i = 0; i < (zip_uint64_t)num_entries; i++) {
        const char *name = zip_get_name(za, i, 0);
        if (!name || !*name || name[strlen(name) - 1] == '/' || strstr(name, "__MACOSX") != NULL)
            continue;

        if (strcasecmp(name, "ComicInfo.xml") == 0 || strcasecmp(name, "comicinfo.xml") == 0) {
            comic_info_idx = (zip_int64_t)i;
            continue;
        }

        if (!is_image_extension(name))
            continue;

        struct zip_stat entry_st;
        zip_stat_init(&entry_st);
        if (zip_stat_index(za, i, 0, &entry_st) != 0 ||
            !(entry_st.valid & ZIP_STAT_SIZE) || entry_st.size == 0 || entry_st.size > SIZE_MAX)
            continue;

        PageMeta *page = &arch->pages[arch->total_pages];
        page->filename = strdup(name);
        if (!page->filename) {
            archive_close(arch);
            return NULL;
        }
        page->index = i;
        page->uncomp_size = entry_st.size;
        page->type = PAGE_TYPE_STORY;
        arch->total_pages++;
    }

    if (arch->total_pages == 0) {
        archive_close(arch);
        return NULL;
    }

    qsort(arch->pages, (size_t)arch->total_pages, sizeof(*arch->pages), compare_natural);

    /* Parse ComicInfo.xml metadata if present */
    arch->info = comicinfo_create();
    if (arch->info && comic_info_idx >= 0) {
        struct zip_stat xml_st;
        zip_stat_init(&xml_st);
        if (zip_stat_index(za, (zip_uint64_t)comic_info_idx, 0, &xml_st) == 0 &&
            (xml_st.valid & ZIP_STAT_SIZE) && xml_st.size > 0 && xml_st.size < 50000000) {
            zip_file_t *zf = zip_fopen_index(za, (zip_uint64_t)comic_info_idx, 0);
        if (zf) {
            char *xml_buf = malloc(xml_st.size + 1);
            if (xml_buf) {
                size_t xread = 0;
                while (xread < xml_st.size) {
                    zip_int64_t r = zip_fread(zf, xml_buf + xread, xml_st.size - xread);
                    if (r <= 0)
                        break;
                    xread += (size_t)r;
                }
                xml_buf[xread] = '\0';
                comicinfo_parse_xml(arch->info, xml_buf, xread, arch->total_pages);
                free(xml_buf);
            }
            zip_fclose(zf);
        }
            }
    }

    return arch;
}

zip_t *archive_open_worker_handle(CBZArchive *arch)
{
    if (!arch || !arch->mmap_base || arch->file_size == 0)
        return NULL;

    zip_error_t zerr;
    zip_error_init(&zerr);
    zip_source_t *src = zip_source_buffer_create(arch->mmap_base, arch->file_size, 0, &zerr);
    if (!src) {
        zip_error_fini(&zerr);
        return NULL;
    }

    zip_t *za = zip_open_from_source(src, ZIP_RDONLY, &zerr);
    if (!za) {
        zip_source_free(src);
    }
    zip_error_fini(&zerr);
    return za;
}

void archive_close_worker_handle(zip_t *za)
{
    if (za)
        zip_close(za);
}

unsigned char *archive_read_file_worker(CBZArchive *arch, zip_t *za, int page_idx, size_t *out_size)
{
    if (!arch || !za || !out_size || page_idx < 0 || page_idx >= arch->total_pages)
        return NULL;

    size_t size = (size_t)arch->pages[page_idx].uncomp_size;
    if (size == 0 || size > (size_t)256 * 1024 * 1024)
        return NULL;

    unsigned char *buffer = malloc(size);
    if (!buffer)
        return NULL;

    zip_file_t *zf = zip_fopen_index(za, arch->pages[page_idx].index, 0);
    if (!zf) {
        free(buffer);
        return NULL;
    }

    /* Loop until the complete entry stream has been read */
    size_t total_read = 0;
    while (total_read < size) {
        zip_int64_t r = zip_fread(zf, buffer + total_read, size - total_read);
        if (r < 0) {
            free(buffer);
            zip_fclose(zf);
            return NULL;
        }
        if (r == 0)
            break;
        total_read += (size_t)r;
    }
    zip_fclose(zf);

    if (total_read != size) {
        free(buffer);
        return NULL;
    }

    *out_size = total_read;
    return buffer;
}

unsigned char *archive_read_file(CBZArchive *arch, int page_idx, size_t *out_size)
{
    if (!arch)
        return NULL;
    pthread_mutex_lock(&arch->lock);
    unsigned char *buf = archive_read_file_worker(arch, arch->za, page_idx, out_size);
    pthread_mutex_unlock(&arch->lock);
    return buf;
}

void archive_close(CBZArchive *arch)
{
    if (!arch)
        return;

    comicinfo_destroy(arch->info);
    for (int i = 0; i < arch->total_pages; i++)
        free(arch->pages[i].filename);
    free(arch->pages);

    pthread_mutex_destroy(&arch->lock);

    if (arch->za)
        zip_close(arch->za);
    if (arch->mmap_base && arch->file_size)
        munmap(arch->mmap_base, arch->file_size);
    if (arch->fd >= 0)
        close(arch->fd);

    free(arch);
}
