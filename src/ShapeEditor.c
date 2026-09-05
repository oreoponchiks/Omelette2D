#include "ShapeEditor.h"
#include "SpriteImport.h"
#include "Ui.h"
#include <commdlg.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static unsigned editorColor(Material material) {
    if (material == Material_Empty)
        return 0xFF252529;
    if (material == Material_Wood)
        return 0xFF3873AA;
    if (material == Material_Plant || material == Material_Moss)
        return 0xFF469637;
    if (material == Material_Stone)
        return 0xFF909090;
    if (material == Material_Glass || material == Material_Ice)
        return 0xFFDFC882;
    return 0xFFB0A090;
}
void shape_editor_init(ShapeEditor* e) {
    memset(e, 0, sizeof(*e));
    e->material = Material_Wood;
    e->brush = 1;
    e->last_x = e->last_y = -1;
    e->seed = GetTickCount();
    sandbox_shape_preset(&e->shape, ShapePreset_Box, Material_Wood);
}
void shape_editor_next_variant(ShapeEditor* e) {
    if (!e->vary)
        return;
    e->seed = e->seed * UINT32_C(1664525) + UINT32_C(1013904223);
    sandbox_shape_generate(&e->shape, (ShapePreset)e->preset, (Material)e->material, e->seed);
}
void shape_editor_preset(ShapeEditor* e, ShapePreset preset) {
    e->preset = preset;
    e->vary = sandbox_shape_varies(preset);
    if (e->vary)
        shape_editor_next_variant(e);
    else
        sandbox_shape_preset(&e->shape, preset, (Material)e->material);
    e->status[0] = 0;
}
bool shape_editor_draw(ShapeEditor* e) {
    bool place = false;
    if (!e->visible)
        return false;
    igSetNextWindowSize((ImVec2_c){380, 650}, ImGuiCond_FirstUseEver);
    igSetNextWindowPos((ImVec2_c){500, 20}, ImGuiCond_FirstUseEver, (ImVec2_c){0, 0});
    if (igBegin("Object workshop", &e->visible, 0)) {
        igInputText("Sprite XML", e->import_path, sizeof(e->import_path), 0, NULL, NULL);
        bool import = false;
        if (igButton("Browse XML...", (ImVec2_c){0, 0})) {
            wchar_t path[32768] = {0};
            OPENFILENAMEW dialog = {0};
            dialog.lStructSize = sizeof(dialog);
            dialog.hwndOwner = GetActiveWindow();
            dialog.lpstrFilter = L"Sprite XML (*.xml)\0*.xml\0\0";
            dialog.lpstrFile = path;
            dialog.nMaxFile = 32768;
            dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
            if (GetOpenFileNameW(&dialog)) {
                if (WideCharToMultiByte(CP_UTF8, 0, path, -1, e->import_path,
                                        sizeof(e->import_path), NULL, NULL))
                    import = true;
                else
                    strcpy_s(e->status, sizeof(e->status), "Selected path is too long.");
            }
        }
        igSameLine(0, -1);
        if (igButton("Import XML", (ImVec2_c){0, 0}))
            import = true;
        if (import &&
            sprite_import_xml(e->import_path, &e->shape, &e->fixed, e->status, sizeof(e->status))) {
            e->vary = false;
            e->preset = ShapePreset_Box;
            e->angle = 0;
            e->last_x = e->last_y = -1;
        }
        igTextWrapped("PNG + XML: up to 48x48 pixels; transparent pixels are empty.");
        if (e->status[0])
            igTextWrapped("%s", e->status);
        igSeparator();
        if (igBeginCombo("Preset", sandbox_shape_name((ShapePreset)e->preset), 0)) {
            for (int n = 0; n < ShapePreset_Count; ++n)
                if (igSelectable_Bool(sandbox_shape_name((ShapePreset)n), e->preset == n, 0,
                                      (ImVec2_c){0, 0})) {
                    e->preset = n;
                    e->vary = false;
                }
            igEndCombo();
        }
        if (igButton("Load preset", (ImVec2_c){0, 0}))
            shape_editor_preset(e, (ShapePreset)e->preset);
        igSameLine(0, -1);
        if (igButton("Clear shape", (ImVec2_c){0, 0})) {
            memset(&e->shape, 0, sizeof(e->shape));
            e->vary = false;
        }
        if (sandbox_shape_varies((ShapePreset)e->preset)) {
            igCheckbox("Vary each placement", &e->vary);
            igSameLine(0, -1);
            if (igButton("New variation", (ImVec2_c){0, 0})) {
                bool vary = e->vary;
                e->vary = true;
                shape_editor_next_variant(e);
                e->vary = vary;
            }
        }
        if (igBeginCombo("Object material", sandbox_material_name((Material)e->material), 0)) {
            for (int n = 1; n < Material_Count; ++n)
                if (sandbox_material_is_rigid((Material)n))
                    if (igSelectable_Bool(sandbox_material_name((Material)n), e->material == n, 0,
                                          (ImVec2_c){0, 0}))
                        e->material = n;
            igEndCombo();
        }
        igSliderInt("Pencil radius", &e->brush, 0, 4, "%d", 0);
        igTextWrapped("Draw a connected shape. Left drag adds pixels; right drag erases. The "
                      "template can be placed repeatedly.");
        ImVec2_c origin = igGetCursorScreenPos();
        float scale = 5.0f, size = Sandbox_ShapeSize * scale;
        igInvisibleButton("Shape canvas", (ImVec2_c){size, size},
                          ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        const ImGuiIO* io = igGetIO_Nil();
        bool painting =
            (igIsItemHovered(0) || igIsItemActive()) && (io->MouseDown[0] || io->MouseDown[1]);
        int x = (int)floorf((io->MousePos.x - origin.x) / scale),
            y = (int)floorf((io->MousePos.y - origin.y) / scale);
        if (painting && x >= 0 && y >= 0 && x < Sandbox_ShapeSize && y < Sandbox_ShapeSize) {
            e->vary = false; /* Preserve a hand-edited template across placements. */
            int from_x = e->last_x < 0 ? x : e->last_x, from_y = e->last_y < 0 ? y : e->last_y;
            int steps = abs(x - from_x) > abs(y - from_y) ? abs(x - from_x) : abs(y - from_y);
            if (steps < 1)
                steps = 1;
            for (int n = 0; n <= steps; ++n)
                sandbox_shape_paint(
                    &e->shape, from_x + (x - from_x) * n / steps, from_y + (y - from_y) * n / steps,
                    io->MouseDown[1] ? Material_Empty : (Material)e->material, e->brush);
            e->last_x = x;
            e->last_y = y;
        } else
            e->last_x = e->last_y = -1;
        ImDrawList* draw = igGetWindowDrawList();
        for (int py = 0; py < Sandbox_ShapeSize; ++py)
            for (int px = 0; px < Sandbox_ShapeSize; ++px)
                ImDrawList_AddRectFilled(
                    draw, (ImVec2_c){origin.x + px * scale, origin.y + py * scale},
                    (ImVec2_c){origin.x + (px + 1) * scale - 0.5f,
                               origin.y + (py + 1) * scale - 0.5f},
                    e->shape.colors[py * Sandbox_ShapeSize + px]
                        ? (0xFF000000u |
                           ((e->shape.colors[py * Sandbox_ShapeSize + px] & 255) << 16) |
                           (e->shape.colors[py * Sandbox_ShapeSize + px] & 0xFF00) |
                           ((e->shape.colors[py * Sandbox_ShapeSize + px] >> 16) & 255))
                        : editorColor((Material)e->shape.pixels[py * Sandbox_ShapeSize + px]),
                    0, 0);
        igSliderFloat("Rotation", &e->angle, -180, 180, "%.0f degrees", 0);
        igCheckbox("Fixed in place", &e->fixed);
        if (e->shape.break_speed > 0)
            igTextWrapped("Use Grab to fling this object. Release while moving: a hard impact "
                          "shatters it into chunks and debris.");
        if (igButton("Place on canvas", (ImVec2_c){0, 0}))
            place = true;
        igTextWrapped("Click clear space in the world to place. Green preview fits; red means "
                      "overlap, disconnected pixels, or the object limit. Grab moves loose "
                      "objects. Erase cuts them apart.");
        if (e->status[0])
            igTextWrapped("%s", e->status);
    }
    igEnd();
    return place;
}
void shape_editor_preview(const ShapeEditor* e, Sandbox* s, int x, int y, float cw, float ch,
                          float ox, float oy) {
    float angle = e->angle * 0.0174532925f, c = cosf(angle), sn = sinf(angle);
    bool fits = sandbox_can_place_shape(s, &e->shape, (float)x, (float)y, angle);
    unsigned color = fits ? 0x884CE880 : 0x88606AFF;
    ImDrawList* draw = igGetForegroundDrawList_Nil();
    /* Use the same inverse rasterization as collision/placement, including holes. */
    for (int wy = y - 35; wy <= y + 35; ++wy)
        for (int wx = x - 35; wx <= x + 35; ++wx) {
            int px = (int)floorf(c * (wx - x) + sn * (wy - y) + 24.0001f);
            int py = (int)floorf(-sn * (wx - x) + c * (wy - y) + 24.0001f);
            if (px < 0 || py < 0 || px >= Sandbox_ShapeSize || py >= Sandbox_ShapeSize ||
                !e->shape.pixels[py * Sandbox_ShapeSize + px])
                continue;
            ImDrawList_AddRectFilled(draw, (ImVec2_c){ox + wx * cw, oy + wy * ch},
                                     (ImVec2_c){ox + (wx + 1) * cw, oy + (wy + 1) * ch}, color, 0,
                                     0);
        }
}
