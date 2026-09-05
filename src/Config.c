#include "Config.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

static char* trim(char* text) {
    while (isspace((unsigned char)*text))
        ++text;
    char* end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1]))
        --end;
    *end = 0;
    return text;
}

static bool copy_string(char* destination, size_t size, const char* value) {
    size_t length = strlen(value);
    if (length < 2 || value[0] != '"' || value[length - 1] != '"' || length - 1 > size)
        return false;
    size_t output = 0;
    for (size_t i = 1; i + 1 < length; ++i) {
        char c = value[i];
        if (c == '\\') {
            if (++i + 1 >= length)
                return false;
            c = value[i];
            if (c == 'n')
                c = '\n';
            else if (c == 't')
                c = '\t';
            else if (c != '\\' && c != '"')
                return false;
        }
        if (output + 1 >= size)
            return false;
        destination[output++] = c;
    }
    destination[output] = 0;
    return true;
}

static bool parse_int(const char* value, int minimum, int maximum, int* output) {
    char* end;
    errno = 0;
    long parsed = strtol(value, &end, 10);
    while (isspace((unsigned char)*end))
        ++end;
    if (errno || *end || !isfinite(parsed) || parsed < minimum || parsed > maximum)
        return false;
    *output = (int)parsed;
    return true;
}

static bool parse_float(const char* value, float minimum, float maximum, float* output) {
    char* end;
    errno = 0;
    float parsed = strtof(value, &end);
    while (isspace((unsigned char)*end))
        ++end;
    if (errno || *end || parsed < minimum || parsed > maximum)
        return false;
    *output = parsed;
    return true;
}

static bool parse_bool(const char* value, bool* output) {
    if (!strcmp(value, "true")) {
        *output = true;
        return true;
    }
    if (!strcmp(value, "false")) {
        *output = false;
        return true;
    }
    return false;
}

void config_defaults(AppConfig* c) {
    memset(c, 0, sizeof(*c));
    strcpy_s(c->title, sizeof(c->title), "Omelette2D - Vulkan");
    c->window_width = 1280;
    c->window_height = 720;
    strcpy_s(c->start_scene, sizeof(c->start_scene), "empty");
    strcpy_s(c->view, sizeof(c->view), "normal");
    strcpy_s(c->material, sizeof(c->material), "Sand");
    strcpy_s(c->tool, sizeof(c->tool), "paint");
    c->brush_radius = 3;
    c->heat_rate = 600.0f;
    c->cool_rate = 600.0f;
    c->pressure_rate = 20.0f;
    c->panel_width = 470.0f;
    c->panel_height = 610.0f;
}

static bool set_value(AppConfig* c, const char* section, const char* key, const char* value) {
    if (!strcmp(section, "window")) {
        if (!strcmp(key, "title")) return copy_string(c->title, sizeof(c->title), value);
        if (!strcmp(key, "width")) return parse_int(value, 320, 7680, &c->window_width);
        if (!strcmp(key, "height")) return parse_int(value, 240, 4320, &c->window_height);
        if (!strcmp(key, "maximized")) return parse_bool(value, &c->maximized);
    } else if (!strcmp(section, "simulation")) {
        if (!strcmp(key, "paused")) return parse_bool(value, &c->paused);
        if (!strcmp(key, "start_scene")) return copy_string(c->start_scene, sizeof(c->start_scene), value);
        if (!strcmp(key, "view")) return copy_string(c->view, sizeof(c->view), value);
    } else if (!strcmp(section, "tools")) {
        if (!strcmp(key, "material")) return copy_string(c->material, sizeof(c->material), value);
        if (!strcmp(key, "selected")) return copy_string(c->tool, sizeof(c->tool), value);
        if (!strcmp(key, "brush_radius")) return parse_int(value, 1, 12, &c->brush_radius);
        if (!strcmp(key, "heat_rate")) return parse_float(value, 0, 100000, &c->heat_rate);
        if (!strcmp(key, "cool_rate")) return parse_float(value, 0, 100000, &c->cool_rate);
        if (!strcmp(key, "pressure_rate")) return parse_float(value, 0, 100000, &c->pressure_rate);
    } else if (!strcmp(section, "ui")) {
        if (!strcmp(key, "panel_width")) return parse_float(value, 250, 2000, &c->panel_width);
        if (!strcmp(key, "panel_height")) return parse_float(value, 250, 2000, &c->panel_height);
    }
    return false;
}

static bool one_of(const char* value, const char* const* options, size_t count) {
    for (size_t i = 0; i < count; ++i)
        if (!_stricmp(value, options[i]))
            return true;
    return false;
}

bool config_load(AppConfig* c, char* error, size_t error_size) {
    config_defaults(c);
    wchar_t path[MAX_PATH];
    DWORD length = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (!length || length >= MAX_PATH) {
        snprintf(error, error_size, "Could not locate config.toml");
        return false;
    }
    wchar_t* slash = wcsrchr(path, L'\\');
    if (slash)
        wcscpy_s(slash + 1, MAX_PATH - (size_t)(slash + 1 - path), L"config.toml");
    FILE* file = NULL;
    if (_wfopen_s(&file, path, L"rb") || !file)
        return true;

    char line[512], section[64] = "";
    unsigned line_number = 0;
    while (fgets(line, sizeof(line), file)) {
        ++line_number;
        char* comment = NULL;
        bool quoted = false;
        for (char* p = line; *p; ++p) {
            if (*p == '"' && (p == line || p[-1] != '\\')) quoted = !quoted;
            if (*p == '#' && !quoted) { comment = p; break; }
        }
        if (comment) *comment = 0;
        char* content = trim(line);
        if (!*content) continue;
        size_t content_length = strlen(content);
        if (content[0] == '[' && content_length > 2 && content[content_length - 1] == ']') {
            content[content_length - 1] = 0;
            char* name = trim(content + 1);
            if (strlen(name) >= sizeof(section)) goto invalid;
            strcpy_s(section, sizeof(section), name);
            continue;
        }
        char* equals = strchr(content, '=');
        if (!equals || !section[0]) goto invalid;
        *equals = 0;
        char* key = trim(content);
        char* value = trim(equals + 1);
        if (!*key || !*value || !set_value(c, section, key, value)) goto invalid;
    }
    if (ferror(file)) {
        snprintf(error, error_size, "Could not read config.toml");
        fclose(file);
        return false;
    }
    fclose(file);
    const char* scenes[] = {"empty", "object_demo"};
    const char* views[] = {"normal", "heat", "pressure"};
    const char* tools[] = {"paint", "heat", "cool", "air", "object", "grab"};
    if (!one_of(c->start_scene, scenes, sizeof(scenes) / sizeof(scenes[0]))) {
        snprintf(error, error_size, "simulation.start_scene has an unknown value");
        return false;
    }
    if (!one_of(c->view, views, sizeof(views) / sizeof(views[0]))) {
        snprintf(error, error_size, "simulation.view has an unknown value");
        return false;
    }
    if (!one_of(c->tool, tools, sizeof(tools) / sizeof(tools[0]))) {
        snprintf(error, error_size, "tools.selected has an unknown value");
        return false;
    }
    return true;

invalid:
    snprintf(error, error_size, "Invalid or unknown config.toml setting on line %u", line_number);
    fclose(file);
    return false;
}
