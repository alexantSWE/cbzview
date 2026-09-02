#include "config.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

static int config_path(char *path, size_t size)
{
    const char *base = getenv("XDG_CONFIG_HOME");
    char dir[PATH_MAX];
    if (base && *base)
        snprintf(dir, sizeof(dir), "%s/cbzview", base);
    else {
        base = getenv("HOME");
        if (!base)
            return 0;
        snprintf(dir, sizeof(dir), "%s/.config/cbzview", base);
    }
    mkdir(dir, 0755);
    return snprintf(path, size, "%s/config", dir) < (int)size;
}

static char *trim(char *text)
{
    while (*text == ' ' || *text == '\t')
        text++;
    char *end = text + strlen(text);
    while (end > text && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n' || end[-1] == '\r'))
        *--end = '\0';
    return text;
}

void config_load(AppConfig *config)
{
    config->auto_resume = 1;
    config->default_layout = LAYOUT_SINGLE;
    config->default_direction = DIR_LTR;
    config->default_fit = FIT_HEIGHT;
    config->contrast_boost = 0;

    char path[PATH_MAX];
    if (!config_path(path, sizeof(path)))
        return;

    FILE *file = fopen(path, "r");
    if (!file) {
        file = fopen(path, "w");
        if (!file)
            return;
        fputs("# cbzview configuration\n"
        "auto_resume = 1\n"
        "default_layout = single\n"
        "default_direction = ltr\n"
        "default_fit = height\n"
        "contrast_boost = 0\n", file);
        fclose(file);
        return;
    }

    char line[256], key[64], value[64];
    while (fgets(line, sizeof(line), file)) {
        if (sscanf(line, " %63[^=]= %63s", key, value) != 2)
            continue;
        char *k = trim(key);
        char *v = trim(value);
        if (strcmp(k, "auto_resume") == 0)
            config->auto_resume = (atoi(v) != 0);
        else if (strcmp(k, "contrast_boost") == 0)
            config->contrast_boost = (atoi(v) != 0);
        else if (strcmp(k, "default_layout") == 0) {
            if (strcasecmp(v, "dual") == 0)
                config->default_layout = LAYOUT_DUAL;
            else if (strcasecmp(v, "webtoon") == 0)
                config->default_layout = LAYOUT_WEBTOON;
        } else if (strcmp(k, "default_direction") == 0)
            config->default_direction = (strcasecmp(v, "rtl") == 0) ? DIR_RTL : DIR_LTR;
        else if (strcmp(k, "default_fit") == 0)
            config->default_fit = (strcasecmp(v, "width") == 0) ? FIT_WIDTH : FIT_HEIGHT;
    }
    fclose(file);
}
