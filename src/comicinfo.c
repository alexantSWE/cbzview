#include "comicinfo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

ComicInfo *comicinfo_create(void)
{
  ComicInfo *info = calloc(1, sizeof(*info));
  return info;
}

static void extract_tag_content(const char *xml, const char *tag, char *out, size_t out_sz)
{
  char open_tag[64], close_tag[64];
  snprintf(open_tag, sizeof(open_tag), "<%s>", tag);
  snprintf(close_tag, sizeof(close_tag), "</%s>", tag);

  const char *start = strcasestr(xml, open_tag);
  if (!start)
    return;
  start += strlen(open_tag);

  const char *end = strcasestr(start, close_tag);
  if (!end)
    return;

  size_t len = (size_t)(end - start);
  if (len >= out_sz)
    len = out_sz - 1;

  memcpy(out, start, len);
  out[len] = '\0';
}

void comicinfo_parse_xml(ComicInfo *info, const char *xml_data, size_t len, int total_pages)
{
  if (!info || !xml_data || len == 0)
    return;

  char *buf = malloc(len + 1);
  if (!buf)
    return;
  memcpy(buf, xml_data, len);
  buf[len] = '\0';

  extract_tag_content(buf, "Title", info->title, sizeof(info->title));
  extract_tag_content(buf, "Series", info->series, sizeof(info->series));
  extract_tag_content(buf, "Volume", info->volume, sizeof(info->volume));
  extract_tag_content(buf, "Number", info->number, sizeof(info->number));

  char manga_tag[64] = {0};
  extract_tag_content(buf, "Manga", manga_tag, sizeof(manga_tag));
  if (strcasecmp(manga_tag, "YesAndRightToLeft") == 0 ||
    strcasecmp(manga_tag, "Yes") == 0 ||
    strcasecmp(manga_tag, "RightToLeft") == 0) {
    info->is_manga = 1;
    }

    info->page_type_count = total_pages;
  info->page_types = calloc((size_t)total_pages, sizeof(ComicPageType));
  if (!info->page_types) {
    free(buf);
    return;
  }

  for (int i = 0; i < total_pages; i++)
    info->page_types[i] = PAGE_TYPE_STORY;

  /* Parse <Page Image="0" Type="FrontCover" ... /> tags */
  const char *ptr = buf;
  while ((ptr = strcasestr(ptr, "<Page ")) != NULL) {
    const char *tag_end = strchr(ptr, '>');
    if (!tag_end)
      break;

    const char *img_attr = strcasestr(ptr, "Image=\"");
    const char *type_attr = strcasestr(ptr, "Type=\"");

    if (img_attr && img_attr < tag_end && type_attr && type_attr < tag_end) {
      int page_idx = atoi(img_attr + 7);
      const char *type_val = type_attr + 6;

      if (page_idx >= 0 && page_idx < total_pages) {
        if (strncasecmp(type_val, "FrontCover", 10) == 0)
          info->page_types[page_idx] = PAGE_TYPE_FRONT_COVER;
        else if (strncasecmp(type_val, "Spread", 6) == 0)
          info->page_types[page_idx] = PAGE_TYPE_SPREAD;
        else if (strncasecmp(type_val, "Deleted", 7) == 0)
          info->page_types[page_idx] = PAGE_TYPE_DELETED;
      }
    }
    ptr = tag_end + 1;
  }

  info->has_metadata = 1;
  free(buf);
}

void comicinfo_destroy(ComicInfo *info)
{
  if (!info)
    return;
  free(info->page_types);
  free(info);
}
