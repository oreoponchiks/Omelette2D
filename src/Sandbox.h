#pragma once

#include "Sprite.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { Sandbox_Width = 320, Sandbox_Height = 180 };
typedef enum Material {
    Material_Empty,
    Material_Sand,
    Material_Water,
    Material_Wood,
    Material_Fire,
    Material_Smoke,
    Material_Stone,
    Material_Oil,
    Material_Lava,
    Material_Acid,
    Material_Ice,
    Material_Plant,
    Material_Steam,
    Material_Mud,
    Material_DilutedAcid,
#define MATERIAL(id, ...) Material_##id,
#include "Materials.def"
#undef MATERIAL
    Material_Count
} Material;
_Static_assert(Material_Count <= UINT8_MAX, "Material IDs must fit in a particle");

typedef enum MaterialCategory {
    MaterialCategory_Tools,
    MaterialCategory_Powders,
    MaterialCategory_Liquids,
    MaterialCategory_Gases,
    MaterialCategory_Solids,
    MaterialCategory_Life,
    MaterialCategory_Explosives,
    MaterialCategory_Count
} MaterialCategory;
typedef struct Sandbox Sandbox;

typedef enum SandboxView { SandboxView_Normal, SandboxView_Heat, SandboxView_Pressure } SandboxView;

typedef struct SandboxSample {
    Material material;
    float temperature;            /* Celsius; ambient is 20 C. */
    float pressure;               /* Relative simulation units. */
    float velocity_x, velocity_y; /* Cells per fixed tick. */
} SandboxSample;

Sandbox* sandbox_create(void);
void sandbox_destroy(Sandbox* sandbox);
void sandbox_update(Sandbox* sandbox, float delta_seconds);
void sandbox_set_tick_rate(Sandbox* sandbox, float ticks_per_second);
void sandbox_paint(Sandbox* sandbox, int grid_x, int grid_y, Material material, int radius);
void sandbox_clear(Sandbox* sandbox);
void sandbox_heat(Sandbox* sandbox, int grid_x, int grid_y, float change, int radius);
void sandbox_add_pressure(Sandbox* sandbox, int grid_x, int grid_y, float amount);
SandboxSample sandbox_sample(const Sandbox* sandbox, int grid_x, int grid_y);
void sandbox_set_view(Sandbox* sandbox, SandboxView view);
/* The returned view remains valid until the next build or destruction. */
const Sprite* sandbox_build_sprites(Sandbox* sandbox, float width, float height, size_t* count);
const char* sandbox_material_name(Material material);

MaterialCategory sandbox_material_category(Material material);
const char* sandbox_category_name(MaterialCategory category);
const char* sandbox_material_description(Material material);
/* Arms existing remote charges; explosions occur on the next simulation step. */
void sandbox_detonate_remote(Sandbox* sandbox);

enum { Sandbox_ShapeSize = 48, Sandbox_ShapeCells = 48 * 48, Sandbox_MaxBodies = 256 };
typedef struct SandboxShape {
    uint8_t pixels[Sandbox_ShapeCells];
    uint32_t colors[Sandbox_ShapeCells]; /* Optional 0xRRGGBB shading; 0x01000000 means explicit black. */
    float break_speed; /* Closing speed needed to fracture after a throw; 0 disables. */
} SandboxShape;
typedef enum ShapePreset {
    ShapePreset_Box,
    ShapePreset_Disc,
    ShapePreset_Triangle,
    ShapePreset_Tree,
    ShapePreset_Boulder,
    ShapePreset_Pine,
    ShapePreset_Birch,
    ShapePreset_Willow,
    ShapePreset_Palm,
    ShapePreset_DeadTree,
    ShapePreset_Crate,
    ShapePreset_Barrel,
    ShapePreset_Bridge,
    ShapePreset_Arch,
    ShapePreset_House,
    ShapePreset_Tower,
    ShapePreset_Count
} ShapePreset;
const char* sandbox_shape_name(ShapePreset preset);
bool sandbox_shape_varies(ShapePreset preset);
typedef struct SandboxBodyInfo {
    uint32_t id;
    float x, y, angle, velocity_x, velocity_y, angular_velocity;
    int pixel_count;
    bool fixed;
} SandboxBodyInfo;
bool sandbox_material_is_rigid(Material material);
void sandbox_shape_preset(SandboxShape* shape, ShapePreset preset, Material material);
void sandbox_shape_generate(SandboxShape* shape, ShapePreset preset, Material material,
                            uint32_t seed);
void sandbox_shape_paint(SandboxShape* shape, int x, int y, Material material, int radius);
/* Placement is transactional: requires a connected solid mask and clear space. */
bool sandbox_can_place_shape(const Sandbox* sandbox, const SandboxShape* shape, float x, float y,
                             float angle);
uint32_t sandbox_create_body(Sandbox* sandbox, const SandboxShape* shape, float x, float y,
                             float angle, bool fixed);
int sandbox_body_count(const Sandbox* sandbox);
uint32_t sandbox_body_at(const Sandbox* sandbox, int x, int y);
bool sandbox_body_info(const Sandbox* sandbox, uint32_t id, SandboxBodyInfo* info);
void sandbox_body_impulse(Sandbox* sandbox, uint32_t id, float x, float y, float impulse_x,
                          float impulse_y);
/* id == 0 releases the grab. A held target acts as a spring during fixed steps. */
void sandbox_body_grab(Sandbox* sandbox, uint32_t id, float x, float y);
/* Replaces the current world with a small tree/boulder/flotation playground. */
void sandbox_load_object_demo(Sandbox* sandbox);
