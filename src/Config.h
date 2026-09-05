#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct AppConfig {
    char title[128];
    int window_width;
    int window_height;
    bool maximized;

    bool paused;
    char start_scene[32];
    char view[32];

    char material[64];
    char tool[32];
    int brush_radius;
    float heat_rate;
    float cool_rate;
    float pressure_rate;

    float panel_width;
    float panel_height;
} AppConfig;

void config_defaults(AppConfig* config);
/* Loads config.toml from the executable's directory. A missing file keeps the defaults. */
bool config_load(AppConfig* config, char* error, size_t error_size);
