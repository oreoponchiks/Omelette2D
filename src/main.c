#include "Sandbox.h"
#include "ShapeEditor.h"
#include "Ui.h"
#include "VulkanRenderer.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <wchar.h>

static VulkanRenderer* renderer;

static bool matchesSearch(const char* text, const char* query) {
    if (!query[0])
        return true;
    for (; *text; ++text) {
        size_t i = 0;
        while (query[i] && text[i] &&
               tolower((unsigned char)query[i]) == tolower((unsigned char)text[i]))
            ++i;
        if (!query[i])
            return true;
    }
    return false;
}

static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (igGetCurrentContext() && ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam))
        return TRUE;
    switch (message) {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED)
            renderer_resized(renderer);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR commandLine, int showCommand) {
    (void)previous;
    const wchar_t* className = L"Omelette2DWindow";
    WNDCLASSEXW wc = {sizeof(wc)};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = windowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = className;
    if (!RegisterClassExW(&wc))
        return 1;
    HWND window =
        CreateWindowExW(0, className, L"Omelette2D - Vulkan", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                        CW_USEDEFAULT, 1280, 720, NULL, NULL, instance, NULL);
    if (!window) {
        UnregisterClassW(className, instance);
        return 1;
    }
    ShowWindow(window, showCommand);

    char error[256] = {0};
    int result = 0;
    Sandbox* sandbox = sandbox_create();
    if (!sandbox) {
        snprintf(error, sizeof(error), "Could not allocate sandbox");
        goto cleanup;
    }
    renderer = renderer_create(window, error, sizeof(error));
    if (!renderer)
        goto cleanup;

    Material selected = Material_Sand;
    int brushRadius = 3;
    bool paused = false;
    int brushTool = 0;
    int displayMode = SandboxView_Normal;
    int category = -1;
    char materialSearch[64] = {0};
    ShapeEditor editor;
    shape_editor_init(&editor);
    if (wcsstr(commandLine, L"--object-demo")) {
        sandbox_load_object_demo(sandbox);
        editor.visible = true;
    }
    uint32_t grabbed = 0;
    LARGE_INTEGER frequency, previousTime;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&previousTime);
    MSG message = {0};
    while (message.message != WM_QUIT) {
        if (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
            continue;
        }
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        float deltaTime =
            (float)((double)(now.QuadPart - previousTime.QuadPart) / (double)frequency.QuadPart);
        previousTime = now;
        if (!paused)
            sandbox_update(sandbox, deltaTime);

        ui_begin_frame();
        igSetNextWindowSize((ImVec2_c){470, 610}, ImGuiCond_FirstUseEver);
        igBegin("Material Lab", NULL, 0);
        igTextUnformatted("Left mouse: selected tool. Right mouse: erase.", NULL);
        if (igBeginCombo("Category",
                         category < 0 ? "All materials"
                                      : sandbox_category_name((MaterialCategory)category),
                         0)) {
            for (int i = -1; i < MaterialCategory_Count; ++i)
                if (igSelectable_Bool(i < 0 ? "All materials"
                                            : sandbox_category_name((MaterialCategory)i),
                                      category == i, 0, (ImVec2_c){0, 0}))
                    category = i;
            igEndCombo();
        }
        igInputTextWithHint("Search", "Material name", materialSearch, sizeof(materialSearch), 0,
                            NULL, NULL);
        if (igBeginChild_Str("Material picker", (ImVec2_c){0, 165}, ImGuiChildFlags_Borders, 0)) {
            int shown = 0;
            for (int i = 0; i < Material_Count; ++i) {
                Material material = (Material)i;
                if ((category >= 0 &&
                     sandbox_material_category(material) != (MaterialCategory)category) ||
                    !matchesSearch(sandbox_material_name(material), materialSearch))
                    continue;
                ++shown;
                if (igSelectable_Bool(sandbox_material_name(material),
                                      selected == material && brushTool == 0, 0,
                                      (ImVec2_c){0, 0})) {
                    selected = material;
                    brushTool = 0;
                }
                if (igIsItemHovered(ImGuiHoveredFlags_DelayNormal))
                    igSetTooltip("%s", sandbox_material_description(material));
            }
            if (!shown)
                igTextUnformatted("No materials match this category and search.", NULL);
        }
        igEndChild();
        igText("Selected: %s | %d materials + eraser", sandbox_material_name(selected),
               Material_Count - 1);
        if (igBeginChild_Str("Material hint", (ImVec2_c){0, 55}, 0, 0))
            igTextWrapped("%s", sandbox_material_description(selected));
        igEndChild();
        if (igButton("Detonate remote bombs", (ImVec2_c){0, 0}))
            sandbox_detonate_remote(sandbox);
        if (paused)
            igTextUnformatted("Paused: timers and detonations wait for Step or Resume.", NULL);
        if (igButton("Shape creator", (ImVec2_c){0, 0}))
            editor.visible = true;
        igSameLine(0, -1);
        if (igButton("Oak", (ImVec2_c){0, 0})) {
            shape_editor_preset(&editor, ShapePreset_Tree);
            editor.angle = 0;
            editor.fixed = false;
            brushTool = 4;
        }
        igSameLine(0, -1);
        if (igButton("Pine", (ImVec2_c){0, 0})) {
            shape_editor_preset(&editor, ShapePreset_Pine);
            editor.angle = 0;
            editor.fixed = false;
            brushTool = 4;
        }
        igSameLine(0, -1);
        if (igButton("Boulder", (ImVec2_c){0, 0})) {
            shape_editor_preset(&editor, ShapePreset_Boulder);
            editor.angle = 0;
            editor.fixed = false;
            brushTool = 4;
        }
        igText("Objects: %d / %d", sandbox_body_count(sandbox), Sandbox_MaxBodies);
        if (igButton("Structures", (ImVec2_c){0, 0})) {
            shape_editor_preset(&editor, ShapePreset_House);
            editor.fixed = true;
            editor.angle = 0;
            editor.visible = true;
            brushTool = 4;
        }
        igSameLine(0, -1);
        if (igButton("Clear + object demo", (ImVec2_c){0, 0})) {
            sandbox_load_object_demo(sandbox);
            editor.status[0] = 0;
        }
        if (editor.status[0] && brushTool == 4)
            igTextWrapped("%s", editor.status);
        const char* tools[] = {"Paint", "Heat", "Cool", "Air", "Object", "Grab"};
        for (int i = 0; i < 6; ++i) {
            if (i)
                igSameLine(0.0f, -1.0f);
            if (igRadioButton_Bool(tools[i], brushTool == i))
                brushTool = i;
        }
        igSliderInt("Brush size", &brushRadius, 1, 12, "%d", 0);
        igCheckbox("Paused", &paused);
        igSameLine(0.0f, -1.0f);
        if (igButton("Clear", (ImVec2_c){0, 0}))
            sandbox_clear(sandbox);
        igSameLine(0.0f, -1.0f);
        if (igButton("Step", (ImVec2_c){0, 0})) {
            paused = true;
            sandbox_update(sandbox, 1.0f / 60.0f);
        }
        const char* views[] = {"Normal view", "Heat view", "Pressure view"};
        for (int i = 0; i < 3; ++i) {
            if (i)
                igSameLine(0.0f, -1.0f);
            if (igRadioButton_Bool(views[i], displayMode == i))
                displayMode = i;
        }
        sandbox_set_view(sandbox, (SandboxView)displayMode);
        char statistics[96];
        const ImGuiIO* io = igGetIO_Nil();
        snprintf(statistics, sizeof(statistics), "%dx%d cells | %.1f FPS", Sandbox_Width,
                 Sandbox_Height, io->Framerate);
        igTextUnformatted(statistics, NULL);
        POINT inspectedCursor = {0};
        RECT inspectedClient = {0};
        GetCursorPos(&inspectedCursor);
        ScreenToClient(window, &inspectedCursor);
        GetClientRect(window, &inspectedClient);
        if (inspectedClient.right > 0 && inspectedClient.bottom > 0 && inspectedCursor.x >= 0 &&
            inspectedCursor.x < inspectedClient.right && inspectedCursor.y >= 0 &&
            inspectedCursor.y < inspectedClient.bottom) {
            SandboxSample sample =
                sandbox_sample(sandbox, inspectedCursor.x * Sandbox_Width / inspectedClient.right,
                               inspectedCursor.y * Sandbox_Height / inspectedClient.bottom);
            snprintf(statistics, sizeof(statistics), "%s | %.1f C | pressure %+.2f",
                     sample.material == Material_Empty ? "Air"
                                                       : sandbox_material_name(sample.material),
                     sample.temperature, sample.pressure);
            igTextUnformatted(statistics, NULL);
        }
        igEnd();
        if (shape_editor_draw(&editor))
            brushTool = 4;

        RECT client = {0};
        GetClientRect(window, &client);
        POINT cursor = {0};
        GetCursorPos(&cursor);
        ScreenToClient(window, &cursor);
        bool cursorOnCanvas = !io->WantCaptureMouse && cursor.x >= 0 && cursor.y >= 0 &&
                              cursor.x < client.right && cursor.y < client.bottom;
        if (!io->MouseDown[0] || brushTool != 5 || !cursorOnCanvas) {
            grabbed = 0;
            sandbox_body_grab(sandbox, 0, 0, 0);
        }
        if (cursorOnCanvas) {
            if (io->KeyCtrl && io->MouseWheel != 0.0f) {
                brushRadius += io->MouseWheel > 0.0f ? 1 : -1;
                if (brushRadius < 1)
                    brushRadius = 1;
                if (brushRadius > 12)
                    brushRadius = 12;
            }
            int gridX = cursor.x * Sandbox_Width / (client.right > 1 ? client.right : 1);
            int gridY = cursor.y * Sandbox_Height / (client.bottom > 1 ? client.bottom : 1);
            if (GetAsyncKeyState(VK_RBUTTON) & 0x8000) {
                sandbox_paint(sandbox, gridX, gridY, Material_Empty, brushRadius);
            } else if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) {
                float toolTime = fminf(deltaTime, 0.05f);
                if (brushTool == 0)
                    sandbox_paint(sandbox, gridX, gridY, selected, brushRadius);
                else if (brushTool == 1 || brushTool == 2)
                    sandbox_heat(sandbox, gridX, gridY, (brushTool == 1 ? 600 : -600) * toolTime,
                                 brushRadius);
                else if (brushTool == 3)
                    sandbox_add_pressure(sandbox, gridX, gridY, 20 * toolTime);
                else if (brushTool == 4 && igIsMouseClicked_Bool(0, false)) {
                    uint32_t id =
                        sandbox_create_body(sandbox, &editor.shape, (float)gridX, (float)gridY,
                                            editor.angle * 0.0174532925f, editor.fixed);
                    if (id)
                        shape_editor_next_variant(&editor);
                    snprintf(editor.status, sizeof(editor.status), "%s",
                             id ? "Object placed. Click again to place another."
                                : "Cannot place: use a connected solid shape in clear space "
                                  "(object limit reached).");
                } else if (brushTool == 5) {
                    if (igIsMouseClicked_Bool(0, false))
                        grabbed = sandbox_body_at(sandbox, gridX, gridY);
                    sandbox_body_grab(sandbox, grabbed, (float)gridX, (float)gridY);
                }
            }
            float cellWidth = (float)client.right / Sandbox_Width;
            float cellHeight = (float)client.bottom / Sandbox_Height;
            if (brushTool == 4) {
                shape_editor_preview(&editor, sandbox, gridX, gridY, cellWidth, cellHeight, 0, 0);
            }
            ImDrawList_AddEllipse(
                igGetForegroundDrawList_Nil(),
                (ImVec2_c){(gridX + 0.5f) * cellWidth, (gridY + 0.5f) * cellHeight},
                (ImVec2_c){(brushRadius + 0.5f) * cellWidth, (brushRadius + 0.5f) * cellHeight},
                UINT32_C(0xDCFFFFFF), 0.0f, 48, 1.0f);
        }
        size_t sprite_count;
        const Sprite* sprites = sandbox_build_sprites(sandbox, (float)client.right,
                                                      (float)client.bottom, &sprite_count);
        if (!renderer_draw(renderer, sprites, sprite_count)) {
            snprintf(error, sizeof(error), "%s", renderer_error(renderer));
            break;
        }
    }

cleanup:
    renderer_destroy(renderer);
    renderer = NULL;
    sandbox_destroy(sandbox);
    if (error[0]) {
        fprintf(stderr, "Omelette2D: %s\n", error);
        MessageBoxA(window, error, "Omelette2D error", MB_OK | MB_ICONERROR);
        result = 1;
    }
    if (IsWindow(window))
        DestroyWindow(window);
    UnregisterClassW(className, instance);
    return result;
}
