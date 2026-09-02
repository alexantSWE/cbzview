#ifndef CONFIG_H
#define CONFIG_H

#include "renderer.h"

typedef struct {
    int auto_resume;
    LayoutMode default_layout;
    ReadDirection default_direction;
    FitMode default_fit;
    int contrast_boost;
} AppConfig;

void config_load(AppConfig *config);

#endif /* CONFIG_H */
