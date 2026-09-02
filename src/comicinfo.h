#ifndef COMICINFO_H
#define COMICINFO_H

#include <stddef.h>

typedef enum {
  PAGE_TYPE_UNKNOWN = 0,
  PAGE_TYPE_FRONT_COVER,
  PAGE_TYPE_STORY,
  PAGE_TYPE_SPREAD,
  PAGE_TYPE_SPREAD_LEFT,
  PAGE_TYPE_SPREAD_RIGHT,
  PAGE_TYPE_BACK_COVER,
  PAGE_TYPE_DELETED
} ComicPageType;

typedef struct {
  char title[128];
  char series[128];
  char volume[32];
  char number[32];
  int is_manga;            /* 1 if RTL / Manga, 0 if LTR / Western */
  int has_metadata;
  ComicPageType *page_types;
  int page_type_count;
} ComicInfo;

ComicInfo *comicinfo_create(void);
void comicinfo_parse_xml(ComicInfo *info, const char *xml_data, size_t len, int total_pages);
void comicinfo_destroy(ComicInfo *info);

#endif /* COMICINFO_H */
