#pragma once
#include "Sandbox.h"
typedef struct ShapeEditor {
    SandboxShape shape;
    bool visible, fixed;
    float angle;
    int preset, material, brush, last_x, last_y;
    char status[128];
    char import_path[4096];
    uint32_t seed;
    bool vary;
} ShapeEditor;
void shape_editor_preset(ShapeEditor* editor, ShapePreset preset);
void shape_editor_next_variant(ShapeEditor* editor);
void shape_editor_init(ShapeEditor* editor);
/* Returns true when the user chooses Place on canvas. */
bool shape_editor_draw(ShapeEditor* editor);
void shape_editor_preview(const ShapeEditor* editor, Sandbox* sandbox, int x, int y,
                          float cell_width, float cell_height, float origin_x, float origin_y);
