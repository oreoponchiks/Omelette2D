#include "Sandbox.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

enum {
    CellCount = Sandbox_Width * Sandbox_Height,
    AirScale = 4,
    AirWidth = Sandbox_Width / AirScale,
    AirHeight = Sandbox_Height / AirScale,
    AirCount = AirWidth * AirHeight,
    SpriteCapacity = CellCount + AirCount
};
typedef enum Phase { Empty, Solid, Powder, Liquid, Gas } Phase;
typedef struct Properties {
    Phase phase;
    float density, gravity, drag, air_drag, conductivity, capacity, temperature;
    int spread;
} Properties;

/* Tuned for this 320x180 lattice and a 60 Hz fixed step. */
static const Properties properties[Material_Count] = {
    {Empty, 0, 0.00f, 0.00f, 0.00f, 0.00f, 1.0f, 20, 0},
    {Powder, 90, 0.24f, 0.97f, 0.02f, 0.09f, 1.0f, 20, 0},   /* Sand */
    {Liquid, 40, 0.16f, 0.96f, 0.03f, 0.15f, 4.0f, 20, 8},   /* Water */
    {Solid, 100, 0.00f, 0.00f, 0.00f, 0.035f, 1.5f, 20, 0},  /* Wood */
    {Gas, 1, -0.045f, 0.94f, 0.10f, 0.12f, 0.5f, 750, 0},    /* Fire */
    {Gas, 2, -0.025f, 0.95f, 0.12f, 0.04f, 0.5f, 100, 0},    /* Smoke */
    {Solid, 150, 0.00f, 0.00f, 0.00f, 0.18f, 2.0f, 20, 0},   /* Stone */
    {Liquid, 25, 0.13f, 0.92f, 0.04f, 0.04f, 1.5f, 20, 4},   /* Oil */
    {Liquid, 80, 0.10f, 0.65f, 0.01f, 0.14f, 2.0f, 1100, 1}, /* Lava */
    {Liquid, 45, 0.15f, 0.94f, 0.03f, 0.10f, 2.0f, 20, 6},   /* Acid */
    {Solid, 100, 0.00f, 0.00f, 0.00f, 0.16f, 2.0f, -15, 0},  /* Ice */
    {Solid, 100, 0.00f, 0.00f, 0.00f, 0.035f, 1.0f, 20, 0},  /* Plant */
    {Gas, 3, -0.035f, 0.95f, 0.10f, 0.08f, 1.5f, 130, 0},    /* Steam */
    {Liquid, 65, 0.12f, 0.55f, 0.01f, 0.08f, 2.5f, 20, 1},   /* Mud */
    {Liquid, 42, 0.15f, 0.95f, 0.03f, 0.12f, 3.0f, 20, 7},   /* Diluted acid */
#define MATERIAL(id, name, category, phase, density, gravity, drag, air, conduct, capacity, temp,  \
                 spread, rgb, hint)                                                                \
    {phase, density, gravity, drag, air, conduct, capacity, temp, spread},
#include "Materials.def"
#undef MATERIAL
};

typedef struct Cell {
    uint8_t material, burning;
    uint16_t life;
    uint32_t tint;
    float temperature, vx, vy, carry_x, carry_y;
} Cell;
typedef struct AirCell {
    float pressure, vx, vy;
} AirCell;

typedef struct RigidBody {
    uint32_t id;
    bool fixed;
    float x, y, angle, vx, vy, omega, pivot_x, pivot_y, mass, inertia, radius;
    int min_x, min_y, max_x, max_y, count;
    float break_speed, impact_speed, impact_x, impact_y, impact_nx, impact_ny;
    float impact_vx, impact_vy, impact_omega;
    bool throw_armed;
    bool geometry_dirty;
    Cell pixels[Sandbox_ShapeCells];
} RigidBody;

struct Sandbox {
    Cell cells[CellCount];
    uint8_t updated[CellCount];
    uint8_t moved[CellCount];
    float heat_delta[CellCount];
    AirCell air[AirCount], air_next[AirCount];
    uint8_t blocked[AirCount];
    Sprite sprites[SpriteCapacity];
    uint32_t random_state;
    float accumulator;
    float tick_seconds;
    SandboxView view;
    RigidBody bodies[Sandbox_MaxBodies];
    uint16_t owner[CellCount]; /* zero: terrain/particles; otherwise body slot + 1 */
    struct {
        uint16_t left, right, count;
    } body_rows[Sandbox_MaxBodies][Sandbox_Height];
    uint16_t local_pixel[CellCount];
    uint8_t body_mask[CellCount], displacement_mask[CellCount];
    uint32_t next_body_id, grabbed_body;
    float grab_x, grab_y, grab_local_x, grab_local_y;
};

static Color colorOf(const Cell* cell, int x, int y);
static float clamp(float value, float low, float high) {
    return value < low ? low : (value > high ? high : value);
}
static bool inBounds(int x, int y) {
    return x >= 0 && x < Sandbox_Width && y >= 0 && y < Sandbox_Height;
}
static int airIndex(int x, int y) {
    return (y / AirScale) * AirWidth + x / AirScale;
}
static uint32_t randomNext(Sandbox* s) {
    uint32_t value = s->random_state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    return s->random_state = value;
}
static bool chance(Sandbox* s, unsigned denominator) {
    return randomNext(s) % denominator == 0;
}
static int direction(Sandbox* s) {
    return (randomNext(s) & 1) ? 1 : -1;
}

static int fuelLife(Material m) {
    switch (m) {
    case Material_Coal:
        return 700;
    case Material_Rubber:
        return 350;
    case Material_Plastic:
        return 200;
    case Material_Sugar:
        return 100;
    case Material_Dust:
        return 35;
    case Material_Sawdust:
        return 80;
    case Material_Seed:
        return 80;
    case Material_Moss:
    case Material_Algae:
        return 100;
    case Material_Fuse:
        return 30;
    default:
        return 0;
    }
}
static float ignition(Material m) {
    switch (m) {
    case Material_Coal:
        return 350;
    case Material_Rubber:
        return 400;
    case Material_Plastic:
        return 250;
    case Material_Sugar:
        return 220;
    default:
        return 180;
    }
}
static int blastRadius(Material m) {
    switch (m) {
    case Material_Gunpowder:
        return 4;
    case Material_Hydrogen:
    case Material_NaturalGas:
        return 5;
    case Material_TNT:
    case Material_FireBomb:
        return 14;
    case Material_TimedBomb:
    case Material_CryoBomb:
        return 16;
    case Material_ImpactBomb:
        return 12;
    case Material_RemoteBomb:
        return 18;
    default:
        return 0;
    }
}

static Cell makeCell(Material material) {
    Cell cell = {0};
    cell.material = (uint8_t)material;
    cell.temperature = properties[material].temperature;
    if (material == Material_Wood)
        cell.life = 300;
    if (material == Material_Plant)
        cell.life = 100;
    if (material == Material_Fire)
        cell.life = 65;
    if (material == Material_Smoke)
        cell.life = 480;
    if (material == Material_Acid)
        cell.life = 16;
    if (material == Material_DilutedAcid)
        cell.life = 4;
    if (fuelLife(material))
        cell.life = (uint16_t)fuelLife(material);
    if (material == Material_TimedBomb || material == Material_FireBomb ||
        material == Material_CryoBomb)
        cell.life = 180;
    return cell;
}

static void changeMaterial(Cell* cell, Material material) {
    float temperature = cell->temperature;
    float vx = cell->vx, vy = cell->vy;
    *cell = makeCell(material);
    cell->temperature = temperature;
    if (properties[material].phase != Solid && material != Material_Empty) {
        cell->vx = vx;
        cell->vy = vy;
    }
}

static Material debris(Material m);
#include "RigidBodies.inc"

Sandbox* sandbox_create(void) {
    Sandbox* s = calloc(1, sizeof(*s));
    if (s) {
        s->tick_seconds = 1.0f / 60.0f;
        sandbox_clear(s);
    }
    return s;
}
void sandbox_destroy(Sandbox* s) {
    free(s);
}
void sandbox_clear(Sandbox* s) {
    memset(s->bodies, 0, sizeof(s->bodies));
    memset(s->owner, 0, sizeof(s->owner));
    s->grabbed_body = 0;
    for (int i = 0; i < CellCount; ++i)
        s->cells[i] = makeCell(Material_Empty);
    for (int x = 0; x < Sandbox_Width; ++x)
        s->cells[(Sandbox_Height - 1) * Sandbox_Width + x] = makeCell(Material_Stone);
    memset(s->air, 0, sizeof(s->air));
    memset(s->air_next, 0, sizeof(s->air_next));
    memset(s->blocked, 0, sizeof(s->blocked));
    memset(s->updated, 0, sizeof(s->updated));
    memset(s->moved, 0, sizeof(s->moved));
    memset(s->heat_delta, 0, sizeof(s->heat_delta));
    s->random_state = UINT32_C(0xA341316C);
    s->accumulator = 0;
}

/* Bounds are clipped before brush arithmetic, including calls from tools/tests. */
static bool brushBounds(int x, int y, int* radius) {
    if (!inBounds(x, y) || *radius < 0)
        return false;
    if (*radius > Sandbox_Width)
        *radius = Sandbox_Width;
    return true;
}
void sandbox_paint(Sandbox* s, int x, int y, Material material, int radius) {
    if ((unsigned)material >= Material_Count || !brushBounds(x, y, &radius))
        return;
    for (int oy = -radius; oy <= radius; ++oy)
        for (int ox = -radius; ox <= radius; ++ox)
            if (inBounds(x + ox, y + oy) && ox * ox + oy * oy <= radius * radius) {
                Cell* cell = &s->cells[(y + oy) * Sandbox_Width + x + ox];
                /* Holding a brush over a particle must not reset its heat or momentum. */
                if (cell->material != material)
                    *cell = makeCell(material);
                int i = (y + oy) * Sandbox_Width + x + ox;
                if (s->owner[i]) {
                    RigidBody* body = &s->bodies[s->owner[i] - 1];
                    Cell* pixel = &body->pixels[s->local_pixel[i]];
                    if (pixel->material != material)
                        body->geometry_dirty = true;
                    *pixel = sandbox_material_is_rigid(material) ? *cell : makeCell(Material_Empty);
                    if (!sandbox_material_is_rigid(material))
                        s->owner[i] = 0;
                }
            }
}
void sandbox_heat(Sandbox* s, int x, int y, float change, int radius) {
    if (!isfinite(change) || !brushBounds(x, y, &radius))
        return;
    for (int oy = -radius; oy <= radius; ++oy)
        for (int ox = -radius; ox <= radius; ++ox)
            if (inBounds(x + ox, y + oy) && ox * ox + oy * oy <= radius * radius) {
                Cell* cell = &s->cells[(y + oy) * Sandbox_Width + x + ox];
                if (cell->material != Material_Empty)
                    cell->temperature = clamp(cell->temperature + change, -273.0f, 2000.0f);
            }
}
void sandbox_add_pressure(Sandbox* s, int x, int y, float amount) {
    if (!inBounds(x, y) || !isfinite(amount))
        return;
    AirCell* air = &s->air[airIndex(x, y)];
    air->pressure = clamp(air->pressure + amount, -20.0f, 20.0f);
}
SandboxSample sandbox_sample(const Sandbox* s, int x, int y) {
    SandboxSample sample = {Material_Empty, 20.0f, 0, 0, 0};
    if (!inBounds(x, y))
        return sample;
    const Cell* cell = &s->cells[y * Sandbox_Width + x];
    sample.material = (Material)cell->material;
    sample.temperature = cell->temperature;
    sample.pressure = s->air[airIndex(x, y)].pressure;
    sample.velocity_x = cell->vx;
    sample.velocity_y = cell->vy;
    return sample;
}
void sandbox_set_view(Sandbox* s, SandboxView view) {
    if ((unsigned)view <= SandboxView_Pressure)
        s->view = view;
}

/* Pairwise heat exchange accumulates into a separate buffer, so scan order does
   not favor one side. Heat capacity affects how quickly each material warms. */
static void exchangeHeat(Sandbox* s, int a, int b) {
    const Cell* first = &s->cells[a];
    const Cell* second = &s->cells[b];
    if (!first->material || !second->material)
        return;
    const Properties* pa = &properties[first->material];
    const Properties* pb = &properties[second->material];
    float conductivity = fminf(pa->conductivity, pb->conductivity);
    float energy = (second->temperature - first->temperature) * conductivity;
    s->heat_delta[a] += energy / pa->capacity;
    s->heat_delta[b] -= energy / pb->capacity;
}
static void updateHeat(Sandbox* s) {
    memset(s->heat_delta, 0, sizeof(s->heat_delta));
    for (int y = 0; y < Sandbox_Height; ++y)
        for (int x = 0; x < Sandbox_Width; ++x) {
            int i = y * Sandbox_Width + x;
            if (!s->cells[i].material)
                continue;
            if (x + 1 < Sandbox_Width)
                exchangeHeat(s, i, i + 1);
            if (y + 1 < Sandbox_Height)
                exchangeHeat(s, i, i + Sandbox_Width);
            const int offsets[4][2] = {{0, -1}, {-1, 0}, {1, 0}, {0, 1}};
            for (int n = 0; n < 4; ++n) {
                int nx = x + offsets[n][0], ny = y + offsets[n][1];
                if (!inBounds(nx, ny) ||
                    s->cells[ny * Sandbox_Width + nx].material == Material_Empty) {
                    float loss = properties[s->cells[i].material].phase == Gas ? 0.0008f : 0.0003f;
                    s->heat_delta[i] += (20.0f - s->cells[i].temperature) * loss;
                }
            }
        }
    for (int i = 0; i < CellCount; ++i)
        s->cells[i].temperature =
            clamp(s->cells[i].temperature + s->heat_delta[i], -273.0f, 2000.0f);
}

/* A damped pressure/velocity wave field on 4x4 tiles. Solid-containing tiles
   block air conservatively, including thin walls. This is deliberately lighter
   than a full fluid solver; pressure is measured in relative simulation units. */
static void updateAir(Sandbox* s) {
    memset(s->blocked, 0, sizeof(s->blocked));
    for (int y = 0; y < Sandbox_Height; ++y)
        for (int x = 0; x < Sandbox_Width; ++x)
            if (properties[s->cells[y * Sandbox_Width + x].material].phase == Solid)
                s->blocked[airIndex(x, y)] = 1;
    for (int y = 0; y < AirHeight; ++y)
        for (int x = 0; x < AirWidth; ++x) {
            int i = y * AirWidth + x;
            if (s->blocked[i]) {
                s->air_next[i] = (AirCell){0};
                continue;
            }
            int left = x > 0 && !s->blocked[i - 1] ? i - 1 : i;
            int right = x + 1 < AirWidth && !s->blocked[i + 1] ? i + 1 : i;
            int up = y > 0 && !s->blocked[i - AirWidth] ? i - AirWidth : i;
            int down = y + 1 < AirHeight && !s->blocked[i + AirWidth] ? i + AirWidth : i;
            AirCell a = s->air[i];
            a.vx = (a.vx - (s->air[right].pressure - s->air[left].pressure) * 0.18f) * 0.97f;
            a.vy = (a.vy - (s->air[down].pressure - s->air[up].pressure) * 0.18f) * 0.97f;
            if (left == i || right == i)
                a.vx = 0;
            if (up == i || down == i)
                a.vy = 0;
            a.vx = clamp(a.vx, -8, 8);
            a.vy = clamp(a.vy, -8, 8);
            s->air_next[i] = a;
        }
    for (int y = 0; y < AirHeight; ++y)
        for (int x = 0; x < AirWidth; ++x) {
            int i = y * AirWidth + x;
            if (s->blocked[i]) {
                s->air[i] = (AirCell){0};
                continue;
            }
            float left = x > 0 ? s->air_next[i - 1].vx : 0;
            float right = x + 1 < AirWidth ? s->air_next[i + 1].vx : 0;
            float up = y > 0 ? s->air_next[i - AirWidth].vy : 0;
            float down = y + 1 < AirHeight ? s->air_next[i + AirWidth].vy : 0;
            AirCell a = s->air_next[i];
            float pl = x > 0 && !s->blocked[i - 1] ? s->air_next[i - 1].pressure : a.pressure;
            float pr =
                x + 1 < AirWidth && !s->blocked[i + 1] ? s->air_next[i + 1].pressure : a.pressure;
            float pu = y > 0 && !s->blocked[i - AirWidth] ? s->air_next[i - AirWidth].pressure
                                                          : a.pressure;
            float pd = y + 1 < AirHeight && !s->blocked[i + AirWidth]
                           ? s->air_next[i + AirWidth].pressure
                           : a.pressure;
            float smoothing = 0.035f * (pl + pr + pu + pd - 4 * a.pressure);
            a.pressure = clamp(
                (a.pressure - 0.22f * (right - left + down - up) + smoothing) * 0.992f, -20, 20);
            s->air[i] = a;
        }
}

static void spawnFlame(Sandbox* s, int x, int y) {
    int dx = direction(s);
    const int offsets[3][2] = {{0, -1}, {dx, 0}, {-dx, 0}};
    for (int n = 0; n < 3; ++n) {
        int nx = x + offsets[n][0], ny = y + offsets[n][1];
        if (!inBounds(nx, ny))
            continue;
        int i = ny * Sandbox_Width + nx;
        if (s->cells[i].material == Material_Empty) {
            s->cells[i] = makeCell(Material_Fire);
            s->cells[i].life = (uint16_t)(35 + randomNext(s) % 30);
            s->updated[i] = 1;
            return;
        }
    }
}

void sandbox_detonate_remote(Sandbox* s) {
    for (int i = 0; i < CellCount; ++i)
        if (s->cells[i].material == Material_RemoteBomb)
            s->cells[i].burning = 2;
}

static Material debris(Material m) {
    switch (m) {
    case Material_Glass:
    case Material_Obsidian:
        return Material_GlassShards;
    case Material_Metal:
    case Material_Copper:
    case Material_Steel:
        return Material_MetalDust;
    case Material_Wood:
        return Material_Sawdust;
    case Material_Ice:
        return Material_Snow;
    case Material_Plant:
    case Material_Moss:
        return Material_Dust;
    default:
        return Material_Gravel;
    }
}
static void explode(Sandbox* s, int cx, int cy, Material bomb) {
    int radius = blastRadius(bomb);
    bodyBlast(s, cx, cy, radius);
    bool cold = bomb == Material_CryoBomb;
    s->cells[cy * Sandbox_Width + cx] = makeCell(Material_Empty);
    for (int y = cy - radius; y <= cy + radius; ++y)
        for (int x = cx - radius; x <= cx + radius; ++x) {
            if (!inBounds(x, y))
                continue;
            float dx = (float)(x - cx), dy = (float)(y - cy);
            float distance = sqrtf(dx * dx + dy * dy);
            if (distance >= radius)
                continue;
            float strength = 1 - distance / radius;
            int i = y * Sandbox_Width + x;
            Cell* cell = &s->cells[i];
            Material m = (Material)cell->material;
            if (blastRadius(m)) {
                if (!cold)
                    cell->burning = 2;
                continue;
            }
            if (m && properties[m].phase == Solid && !cold) {
                float resistance =
                    m == Material_Steel
                        ? 0.85f
                        : ((m == Material_Brick || m == Material_Concrete || m == Material_Obsidian)
                               ? 0.65f
                               : 0.28f);
                if (strength > resistance)
                    changeMaterial(cell, debris(m));
            }
            if (!cell->material && chance(s, bomb == Material_FireBomb ? 2 : 6)) {
                *cell = makeCell(cold ? Material_Helium : Material_Fire);
                cell->temperature = cold ? -220.0f : 900.0f;
            }
            if (cell->material) {
                cell->temperature =
                    cold ? fminf(cell->temperature, -250 * strength)
                         : clamp(cell->temperature +
                                     strength * (bomb == Material_FireBomb ? 1500 : 900),
                                 -273, 2000);
                if (properties[cell->material].phase != Solid) {
                    float inverse = distance > 0 ? 1 / distance : 0;
                    cell->vx = clamp(cell->vx + dx * inverse * strength * 6, -6, 6);
                    cell->vy = clamp(cell->vy + dy * inverse * strength * 6, -6, 6);
                }
            }
            s->updated[i] = 1;
        }
    /* One impulse per air tile, independent of particle density. */
    for (int ay = 0; ay < AirHeight; ++ay)
        for (int ax = 0; ax < AirWidth; ++ax) {
            float dx = ax * AirScale + AirScale * 0.5f - cx;
            float dy = ay * AirScale + AirScale * 0.5f - cy;
            float distance = sqrtf(dx * dx + dy * dy);
            if (distance >= radius + AirScale)
                continue;
            float strength = 1 - distance / (radius + AirScale);
            AirCell* air = &s->air[ay * AirWidth + ax];
            air->pressure = clamp(air->pressure + strength * (cold ? 5 : 12), -20, 20);
            float inverse = distance > 0 ? 1 / distance : 0;
            air->vx = clamp(air->vx + dx * inverse * strength * 6, -8, 8);
            air->vy = clamp(air->vy + dy * inverse * strength * 6, -8, 8);
        }
}
static void processExplosions(Sandbox* s) {
    /* Snapshot up to 64 charges per tick: chains continue next tick, never recurse.
       Arming
     * lives on the particle, so pending charges can move safely. */
    int pending[64], count = 0;
    for (int i = 0; i < CellCount && count < 64; ++i)
        if (s->cells[i].burning == 2 && blastRadius((Material)s->cells[i].material))
            pending[count++] = i;
    for (int n = 0; n < count; ++n) {
        int i = pending[n];
        explode(s, i % Sandbox_Width, i / Sandbox_Width, (Material)s->cells[i].material);
    }
}
static void emitGas(Sandbox* s, int x, int y, Material gas, float temperature) {
    const int offsets[4][2] = {{0, -1}, {-1, 0}, {1, 0}, {0, 1}};
    for (int n = 0; n < 4; ++n) {
        int nx = x + offsets[n][0], ny = y + offsets[n][1];
        if (!inBounds(nx, ny))
            continue;
        int i = ny * Sandbox_Width + nx;
        if (s->cells[i].material)
            continue;
        s->cells[i] = makeCell(gas);
        s->cells[i].temperature = temperature;
        s->updated[i] = 1;
        return;
    }
}
static void thermalReaction(Cell* cell) {
    Material target = Material_Empty;
    float t = cell->temperature;
    switch ((Material)cell->material) {
    case Material_Snow:
        if (t > 2)
            target = Material_Water;
        break;
    case Material_Gravel:
        if (t > 1050)
            target = Material_Lava;
        break;
    case Material_Clay:
        if (t > 650)
            target = Material_Brick;
        break;
    case Material_Rust:
    case Material_MetalDust:
    case Material_Metal:
        if (t > 1250)
            target = Material_MoltenMetal;
        break;
    case Material_Copper:
        if (t > 1080)
            target = Material_MoltenMetal;
        break;
    case Material_Steel:
        if (t > 1500)
            target = Material_MoltenMetal;
        break;
    case Material_Glass:
    case Material_GlassShards:
        if (t > 1400)
            target = Material_MoltenGlass;
        break;
    case Material_MoltenMetal:
        if (t < 1150)
            target = Material_Metal;
        break;
    case Material_MoltenGlass:
        if (t < 1250)
            target = Material_Glass;
        break;
    case Material_Wax:
        if (t > 60)
            target = Material_LiquidWax;
        break;
    case Material_LiquidWax:
        if (t < 45)
            target = Material_Wax;
        break;
    case Material_Brick:
    case Material_Concrete:
        if (t > 1600)
            target = Material_Lava;
        break;
    case Material_Obsidian:
        if (t > 1500)
            target = Material_Lava;
        break;
    case Material_LiquidNitrogen:
        if (t > -180)
            target = Material_Helium;
        break;
    case Material_DryIce:
        if (t > -75)
            target = Material_CarbonDioxide;
        break;
    case Material_Honey:
        if (t > 180)
            target = Material_Coal;
        break;
    default:
        break;
    }
    if (target != Material_Empty)
        changeMaterial(cell, target);
}

static void reactCell(Sandbox* s, int x, int y) {
    int i = y * Sandbox_Width + x;
    Cell* cell = &s->cells[i];
    if (!cell->material || s->updated[i])
        return;
    s->updated[i] = 1;
    Material material = (Material)cell->material;
    if (blastRadius(material)) {
        bool timed = material == Material_TimedBomb || material == Material_FireBomb ||
                     material == Material_CryoBomb;
        if (timed && cell->life && --cell->life == 0)
            cell->burning = 2;
        float trigger = material == Material_Gunpowder || material == Material_Hydrogen
                            ? 180.0f
                            : (material == Material_NaturalGas ? 200.0f : 300.0f);
        if (cell->temperature > trigger)
            cell->burning = 2;
        return;
    }
    const int offsets[4][2] = {{0, -1}, {1, 0}, {-1, 0}, {0, 1}};
    bool wet = false;
    bool soil = false;
    for (int n = 0; n < 4; ++n) {
        int nx = x + offsets[n][0], ny = y + offsets[n][1];
        if (inBounds(nx, ny)) {
            Material m = (Material)s->cells[ny * Sandbox_Width + nx].material;
            soil |= m == Material_Soil || m == Material_Mud;
        }
    }
    for (int n = 0; n < 4; ++n) {
        int nx = x + offsets[n][0], ny = y + offsets[n][1];
        if (!inBounds(nx, ny))
            continue;
        int ni = ny * Sandbox_Width + nx;
        Cell* neighbor = &s->cells[ni];
        if (neighbor->material == Material_Water || neighbor->material == Material_SaltWater ||
            neighbor->material == Material_SugarWater ||
            neighbor->material == Material_CarbonDioxide)
            wet = true;
        if (neighbor->material == Material_Water) {
            Material solution = material == Material_Salt
                                    ? Material_SaltWater
                                    : ((material == Material_Sugar || material == Material_Honey)
                                           ? Material_SugarWater
                                           : Material_Empty);
            if (solution) {
                float average = (cell->temperature + neighbor->temperature) * 0.5f;
                changeMaterial(cell, solution);
                changeMaterial(neighbor, solution);
                cell->temperature = neighbor->temperature = average;
                s->updated[ni] = 1;
                return;
            }
            if (material == Material_Clay || material == Material_Soil) {
                changeMaterial(cell, Material_Mud);
                *neighbor = makeCell(Material_Empty);
                s->updated[ni] = 1;
                return;
            }
            if (material == Material_Seed && soil && cell->temperature > 0 &&
                cell->temperature < 80) {
                changeMaterial(cell, Material_Plant);
                *neighbor = makeCell(Material_Empty);
                s->updated[ni] = 1;
                return;
            }
            if ((material == Material_Moss || material == Material_Algae) && !cell->burning &&
                cell->temperature > 0 && cell->temperature < 80 &&
                chance(s, material == Material_Moss ? 200 : 120)) {
                changeMaterial(neighbor, material);
                s->updated[ni] = 1;
            }
            if (material == Material_Metal && chance(s, 600)) {
                changeMaterial(cell, Material_Rust);
                return;
            }
        }
        if (material == Material_Oxygen && neighbor->material == Material_Fire) {
            neighbor->temperature = fminf(neighbor->temperature + 80, 1500);
            neighbor->life = (uint16_t)(neighbor->life < 180 ? neighbor->life + 30 : 210);
            *cell = makeCell(Material_Empty);
            sandbox_add_pressure(s, x, y, 0.12f);
            return;
        }
        if (material == Material_Fuse && cell->burning && neighbor->material == Material_Fuse &&
            !neighbor->burning) {
            neighbor->temperature = fmaxf(neighbor->temperature, 400);
            neighbor->burning = 1;
            s->updated[ni] = 1; /* Propagate one cell per tick in either scan direction. */
        }
        if (material == Material_Acid && neighbor->material == Material_Water) {
            float average = (cell->temperature + neighbor->temperature) * 0.5f;
            changeMaterial(cell, Material_DilutedAcid);
            changeMaterial(neighbor, Material_DilutedAcid);
            cell->temperature = neighbor->temperature = average;
            s->updated[ni] = 1;
            return;
        }
        if ((material == Material_Acid || material == Material_DilutedAcid) && cell->life) {
            bool organic = neighbor->material == Material_Wood ||
                           neighbor->material == Material_Plant ||
                           fuelLife((Material)neighbor->material) > 0;
            bool mineral = material == Material_Acid && (neighbor->material == Material_Stone ||
                                                         neighbor->material == Material_Sand);
            if ((organic || mineral) &&
                chance(s, mineral ? 120 : (material == Material_Acid ? 24 : 90))) {
                *neighbor = makeCell(Material_Empty);
                s->updated[ni] = 1;
                if (--cell->life == 0) {
                    changeMaterial(cell, Material_Water);
                    return;
                }
            }
        }
        if (material == Material_Plant && !cell->burning && neighbor->material == Material_Water &&
            n != 3 && chance(s, 160)) {
            changeMaterial(neighbor, Material_Plant); /* Growth consumes the water cell. */
            s->updated[ni] = 1;
        }
    }
    if (material >= Material_Salt) {
        float pressure = fabsf(s->air[airIndex(x, y)].pressure);
        /* Solids block their own air tile, so inspect adjacent tiles for stress. */
        if (material == Material_Glass || material == Material_Concrete)
            for (int n = 0; n < 4; ++n) {
                int nx = x + offsets[n][0] * AirScale, ny = y + offsets[n][1] * AirScale;
                if (inBounds(nx, ny))
                    pressure = fmaxf(pressure, fabsf(s->air[airIndex(nx, ny)].pressure));
            }
        if ((material == Material_Glass && pressure > 3) ||
            (material == Material_Concrete && pressure > 8)) {
            changeMaterial(cell, debris(material));
            return;
        }
        if ((material == Material_SaltWater || material == Material_SugarWater) &&
            cell->temperature > 110) {
            changeMaterial(cell, material == Material_SaltWater ? Material_Salt : Material_Sugar);
            emitGas(s, x, y, Material_Steam, 120);
            sandbox_add_pressure(s, x, y, 0.3f);
            return;
        }
        if ((material == Material_Alcohol && cell->temperature > 160) ||
            (material == Material_Gasoline && cell->temperature > 120) ||
            (material == Material_LiquidWax && cell->temperature > 300)) {
            changeMaterial(cell, Material_Fire);
            cell->temperature = 800;
            cell->life = 100;
            sandbox_add_pressure(s, x, y, material == Material_Gasoline ? 2.0f : 0.6f);
            return;
        }
        if (fuelLife(material)) {
            if (wet) {
                cell->burning = 0;
                cell->temperature = fminf(cell->temperature, 120);
            } else if (cell->temperature > ignition(material))
                cell->burning = 1;
            if (cell->burning) {
                cell->temperature = fminf(cell->temperature + 15, 800);
                if (chance(s, 3))
                    spawnFlame(s, x, y);
                if (cell->life && --cell->life == 0)
                    changeMaterial(cell, material == Material_Rubber || material == Material_Plastic
                                             ? Material_Smoke
                                             : Material_Ash);
            }
        }
        thermalReaction(cell);
        return;
    }
    if (material == Material_Water) {
        if (cell->temperature >= 105) {
            changeMaterial(cell, Material_Steam);
            cell->temperature -= 3; /* Leave room above the condensation threshold. */
            sandbox_add_pressure(s, x, y, 0.3f);
        } else if (cell->temperature <= -2) {
            changeMaterial(cell, Material_Ice);
            cell->temperature += 2;
        }
    } else if (material == Material_Ice && cell->temperature >= 2) {
        changeMaterial(cell, Material_Water);
        cell->temperature -= 2;
    } else if (material == Material_Steam && cell->temperature < 95) {
        changeMaterial(cell, Material_Water);
        sandbox_add_pressure(s, x, y, -0.12f);
    } else if (material == Material_Lava && cell->temperature < 700) {
        changeMaterial(cell, Material_Stone);
    } else if ((material == Material_Stone && cell->temperature > 1050) ||
               (material == Material_Sand && cell->temperature > 1700)) {
        changeMaterial(cell, Material_Lava);
    } else if (material == Material_Mud && cell->temperature > 100) {
        changeMaterial(cell, Material_Sand);
    } else if (material == Material_Oil && cell->temperature > 230) {
        changeMaterial(cell, Material_Fire);
        cell->temperature = 750;
        cell->life = 90;
        sandbox_add_pressure(s, x, y, 1.2f);
    } else if (material == Material_Wood || material == Material_Plant) {
        if (wet) {
            cell->burning = 0;
            cell->temperature = fminf(cell->temperature, 120);
        } else if (cell->temperature > (material == Material_Wood ? 280 : 180)) {
            cell->burning = 1;
        }
        if (cell->burning) {
            if (cell->temperature < 150) {
                cell->burning = 0;
                return;
            }
            cell->temperature = fminf(cell->temperature + 10, 600);
            if (chance(s, 4))
                spawnFlame(s, x, y);
            if (cell->life && --cell->life == 0)
                changeMaterial(cell, Material_Smoke);
        }
    } else if (material == Material_Fire) {
        if (wet || cell->temperature < 180) {
            changeMaterial(cell, Material_Smoke);
            cell->temperature = 90;
            cell->life = 100;
            return;
        }
        cell->temperature = fminf(cell->temperature + 4, 850);
        sandbox_add_pressure(s, x, y, 0.008f);
        AirCell* air = &s->air[airIndex(x, y)];
        air->vy = fmaxf(air->vy - 0.015f, -8);
        if (cell->life && --cell->life == 0)
            changeMaterial(cell, Material_Smoke);
    } else if (material == Material_Smoke) {
        if (cell->life && --cell->life == 0)
            *cell = makeCell(Material_Empty);
    }
}

static bool canEnter(const Sandbox* s, const Cell* source, int x, int y, int dy) {
    if (!inBounds(x, y))
        return false;
    int index = y * Sandbox_Width + x;
    const Cell* target = &s->cells[index];
    if (target->material == Material_Empty)
        return true;
    if (s->moved[index])
        return false;
    const Properties* a = &properties[source->material];
    const Properties* b = &properties[target->material];
    if (b->phase != Liquid && b->phase != Gas)
        return false;
    if (dy > 0)
        return a->density > b->density;
    if (dy < 0 && a->phase == Gas)
        return a->density < b->density;
    return false;
}
static bool moveOne(Sandbox* s, int* x, int* y, int nx, int ny) {
    int from = *y * Sandbox_Width + *x;
    Cell* source = &s->cells[from];
    if (!canEnter(s, source, nx, ny, ny - *y))
        return false;
    if (nx != *x && ny != *y && !canEnter(s, source, nx, *y, 0) &&
        !canEnter(s, source, *x, ny, ny - *y))
        return false;
    int to = ny * Sandbox_Width + nx;
    Cell temporary = s->cells[to];
    s->cells[to] = *source;
    s->cells[from] = temporary;
    if (temporary.material) {
        /* Displaced fluid starts at rest instead of inheriting a downward jet. */
        s->cells[from].vx = s->cells[from].vy = 0;
        s->cells[from].carry_x = s->cells[from].carry_y = 0;
    }
    s->updated[from] = s->updated[to] = 1;
    s->moved[from] = s->moved[to] = 1;
    *x = nx;
    *y = ny;
    return true;
}

static void moveParticle(Sandbox* s, int x, int y) {
    int index = y * Sandbox_Width + x;
    Cell* cell = &s->cells[index];
    if (s->updated[index])
        return;
    s->updated[index] = 1;
    const Properties* p = &properties[cell->material];
    if (p->phase == Solid || p->phase == Empty)
        return;
    AirCell air = s->air[airIndex(x, y)];
    cell->vx = clamp(cell->vx * p->drag + air.vx * p->air_drag, -6, 6);
    cell->vy = clamp(cell->vy * p->drag + p->gravity + air.vy * p->air_drag, -6, 6);
    if (p->phase == Gas)
        cell->vx = clamp(cell->vx + direction(s) * 0.08f, -6, 6);
    float mx = cell->carry_x + cell->vx, my = cell->carry_y + cell->vy;
    int dx = (int)mx, dy = (int)my;
    cell->carry_x = mx - dx;
    cell->carry_y = my - dy;
    int steps = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
    int start_x = x, start_y = y;
    bool collision = false;
    float impact = fabsf(cell->vy);
    /* Sweep through every crossed lattice cell; never jump through a wall. */
    for (int n = 1; n <= steps; ++n) {
        int nx = start_x + (int)roundf((float)dx * n / steps);
        int ny = start_y + (int)roundf((float)dy * n / steps);
        if (nx == x && ny == y)
            continue;
        if (!moveOne(s, &x, &y, nx, ny)) {
            collision = true;
            break;
        }
    }
    cell = &s->cells[y * Sandbox_Width + x];
    if (collision) {
        if (cell->material == Material_ImpactBomb && impact > 1.0f)
            cell->burning = 2;
        cell->carry_x = cell->carry_y = 0;
        cell->vy = 0;
        cell->vx *= 0.3f;
    }
    int side = fabsf(cell->vx) > 0.15f ? (cell->vx > 0 ? 1 : -1) : direction(s);
    if (p->phase == Gas) {
        if (collision) {
            if (!moveOne(s, &x, &y, x + side, y - 1))
                moveOne(s, &x, &y, x + side, y);
        }
        return;
    }
    bool supported = !canEnter(s, cell, x, y + 1, 1);
    if (!supported)
        return;
    if (moveOne(s, &x, &y, x + side, y + 1) || moveOne(s, &x, &y, x - side, y + 1)) {
        s->cells[y * Sandbox_Width + x].vx += side * 0.12f;
        return;
    }
    cell = &s->cells[y * Sandbox_Width + x];
    if (p->phase == Powder) {
        if (collision && impact > 1.5f)
            cell->vx = side * fminf(impact * 0.3f, 1.5f);
        else
            cell->vx *= 0.4f;
        return;
    }
    /* Viscous liquids move laterally less often and travel shorter distances. */
    if (p->spread == 1 && !chance(s, 3))
        return;
    for (int pass = 0; pass < 2; ++pass) {
        int flow = pass == 0 ? side : -side;
        bool moved = false;
        for (int distance = 0; distance < p->spread; ++distance) {
            if (!moveOne(s, &x, &y, x + flow, y))
                break;
            moved = true;
            if (canEnter(s, &s->cells[y * Sandbox_Width + x], x, y + 1, 1))
                break;
        }
        if (moved) {
            s->cells[y * Sandbox_Width + x].vx = flow * 0.45f;
            break;
        }
    }
}

static void step(Sandbox* s) {
    updateAir(s);
    updateHeat(s);
    memset(s->updated, 0, sizeof(s->updated));
    for (int y = 0; y < Sandbox_Height; ++y)
        for (int x = 0; x < Sandbox_Width; ++x)
            reactCell(s, x, y);
    processExplosions(s);
    updateBodies(s);
    memset(s->updated, 0, sizeof(s->updated));
    /* Falling matter and rising gas use opposite scan directions. */
    memset(s->moved, 0, sizeof(s->moved));
    for (int pass = 0; pass < 2; ++pass)
        for (int row = 0; row < Sandbox_Height; ++row) {
            int y = pass == 0 ? Sandbox_Height - 1 - row : row;
            int side = direction(s);
            for (int column = 0; column < Sandbox_Width; ++column) {
                int x = side > 0 ? column : Sandbox_Width - 1 - column;
                Phase phase = properties[s->cells[y * Sandbox_Width + x].material].phase;
                if ((pass == 0 && (phase == Powder || phase == Liquid)) ||
                    (pass == 1 && phase == Gas))
                    moveParticle(s, x, y);
            }
        }
}
void sandbox_set_tick_rate(Sandbox* s, float ticks_per_second) {
    if (s && isfinite(ticks_per_second) && ticks_per_second >= 10.0f && ticks_per_second <= 240.0f)
        s->tick_seconds = 1.0f / ticks_per_second;
}
void sandbox_update(Sandbox* s, float deltaSeconds) {
    if (!isfinite(deltaSeconds) || deltaSeconds <= 0)
        return;
    s->accumulator += fminf(deltaSeconds, 0.1f);
    const float tick = s->tick_seconds;
    while (s->accumulator >= tick) {
        step(s);
        s->accumulator -= tick;
    }
}

static Color heatColor(float temperature) {
    float t = clamp((temperature + 20) / 1000, 0, 1);
    return (Color){clamp(t * 3, 0, 1), clamp(t * 3 - 1, 0, 1), clamp(1 - t * 3, 0, 1), 1};
}
const Sprite* sandbox_build_sprites(Sandbox* s, float width, float height, size_t* count) {
    *count = 0;
    if (!isfinite(width) || !isfinite(height) || width <= 0 || height <= 0)
        return s->sprites;
    float cw = width / Sandbox_Width, ch = height / Sandbox_Height;
    if (s->view == SandboxView_Pressure)
        for (int y = 0; y < AirHeight; ++y)
            for (int x = 0; x < AirWidth; ++x) {
                float pressure = s->air[y * AirWidth + x].pressure;
                float strength = clamp(fabsf(pressure) * 0.5f, 0, 0.85f);
                if (strength < 0.005f)
                    continue;
                Color color = pressure > 0 ? (Color){1, 0.18f, 0.04f, strength}
                                           : (Color){0.08f, 0.3f, 1, strength};
                s->sprites[(*count)++] = (Sprite){x * AirScale * cw, y * AirScale * ch,
                                                  AirScale * cw, AirScale * ch, color};
            }
    for (int y = 0; y < Sandbox_Height; ++y)
        for (int x = 0; x < Sandbox_Width; ++x) {
            const Cell* cell = &s->cells[y * Sandbox_Width + x];
            if (!cell->material)
                continue;
            Color color =
                s->view == SandboxView_Heat ? heatColor(cell->temperature) : colorOf(cell, x, y);
            if (s->view != SandboxView_Heat) {
                if (cell->burning) {
                    color.r = 0.8f;
                    color.g = 0.22f;
                    color.b = 0.03f;
                }
                if (cell->material == Material_Smoke)
                    color.a *= clamp(cell->life / 100.0f, 0.1f, 1);
            }
            s->sprites[(*count)++] = (Sprite){x * cw, y * ch, cw + 0.25f, ch + 0.25f, color};
        }
    return s->sprites;
}

typedef struct MaterialInfo {
    const char* name;
    MaterialCategory category;
    unsigned rgb;
    const char* description;
} MaterialInfo;
static const MaterialInfo materialInfo[Material_Count] = {
    {"Eraser", MaterialCategory_Tools, 0, "Removes particles. Right mouse always erases."},
    {"Sand", MaterialCategory_Powders, 0,
     "Falls, piles up, sinks through water; melts above 1700 C."},
    {"Water", MaterialCategory_Liquids, 0,
     "Flows, quenches fire, freezes and boils; dissolves salt and sugar."},
    {"Wood", MaterialCategory_Solids, 0, "Burns in place above 280 C, emitting fire and smoke."},
    {"Fire", MaterialCategory_Gases, 0, "Hot rising flame; heats and ignites nearby matter."},
    {"Smoke", MaterialCategory_Gases, 0, "Rising combustion gas that gradually fades away."},
    {"Stone", MaterialCategory_Solids, 0,
     "Solid wall; melts above 1050 C and breaks into gravel in blasts."},
    {"Oil", MaterialCategory_Liquids, 0, "Floats on water and ignites above 230 C."},
    {"Lava", MaterialCategory_Liquids, 0, "Hot viscous liquid; solidifies below 700 C."},
    {"Acid", MaterialCategory_Liquids, 0,
     "Dissolves organic matter and some minerals; water dilutes its finite strength."},
    {"Ice", MaterialCategory_Solids, 0, "Frozen solid; melts above 2 C."},
    {"Plant", MaterialCategory_Life, 0, "Consumes water to grow; burns above 180 C."},
    {"Steam", MaterialCategory_Gases, 0, "Warm rising gas; condenses below 95 C."},
    {"Mud", MaterialCategory_Liquids, 0, "Viscous wet earth; dries into sand above 100 C."},
    {"Diluted Acid", MaterialCategory_Liquids, 0,
     "Weak acid with limited organic dissolution; cannot multiply in water."},
#define MATERIAL(id, name, category, phase, density, gravity, drag, air, conduct, capacity, temp,  \
                 spread, rgb, hint)                                                                \
    {name, MaterialCategory_##category, rgb, hint},
#include "Materials.def"
#undef MATERIAL
};
const char* sandbox_material_name(Material material) {
    return (unsigned)material < Material_Count ? materialInfo[material].name : "Unknown";
}
const char* sandbox_material_description(Material material) {
    return (unsigned)material < Material_Count ? materialInfo[material].description : "";
}
MaterialCategory sandbox_material_category(Material material) {
    return (unsigned)material < Material_Count ? materialInfo[material].category
                                               : MaterialCategory_Tools;
}
const char* sandbox_category_name(MaterialCategory category) {
    static const char* const names[] = {"Tools",  "Powders", "Liquids",   "Gases",
                                        "Solids", "Life",    "Explosives"};
    return (unsigned)category < MaterialCategory_Count ? names[category] : "Unknown";
}

static Color colorOf(const Cell* cell, int x, int y) {
    if (cell->tint)
        return (Color){((cell->tint >> 16) & 255) / 255.0f, ((cell->tint >> 8) & 255) / 255.0f,
                       (cell->tint & 255) / 255.0f, 1};
    float variation = (float)((x * 17 + y * 31) & 7) / 100.0f;
    if (cell->material >= Material_Salt && cell->material < Material_Count) {
        unsigned rgb = materialInfo[cell->material].rgb;
        float flicker =
            (cell->material >= Material_TimedBomb && cell->life && (cell->life / 10) % 2) ? 0.3f
                                                                                          : 0;
        return (Color){clamp(((rgb >> 16) & 255) / 255.0f + variation + flicker, 0, 1),
                       clamp(((rgb >> 8) & 255) / 255.0f + variation, 0, 1),
                       clamp((rgb & 255) / 255.0f + variation, 0, 1),
                       cell->material == Material_Glass
                           ? 0.65f
                           : (properties[cell->material].phase == Gas ? 0.6f : 1)};
    }
    switch (cell->material) {
    case Material_Sand:
        return (Color){0.92f + variation, 0.72f + variation, 0.28f, 1};
    case Material_Water:
        return (Color){0.10f, 0.38f + variation, 0.92f, 0.82f};
    case Material_Wood:
        return (Color){0.42f + variation, 0.20f, 0.07f, 1};
    case Material_Fire:
        return (Color){1.0f, 0.25f + variation * 4, 0.03f, 1};
    case Material_Smoke:
        return (Color){0.35f + variation, 0.36f + variation, 0.40f + variation, 0.65f};
    case Material_Stone:
        return (Color){0.35f + variation, 0.36f + variation, 0.39f + variation, 1};
    case Material_Oil:
        return (Color){0.24f + variation, 0.18f + variation, 0.08f, 0.95f};
    case Material_Lava:
        return (Color){1.0f, 0.10f + variation * 4, 0.01f, 1};
    case Material_Acid:
        return (Color){0.25f, 0.95f, 0.10f + variation, 0.9f};
    case Material_Ice:
        return (Color){0.60f + variation, 0.88f, 1.0f, 0.95f};
    case Material_Plant:
        return (Color){0.12f, 0.50f + variation, 0.10f, 1};
    case Material_Steam:
        return (Color){0.72f + variation, 0.80f + variation, 0.88f + variation, 0.55f};
    case Material_Mud:
        return (Color){0.30f + variation, 0.18f + variation, 0.07f, 1.0f};
    case Material_DilutedAcid:
        return (Color){0.18f, 0.67f + variation, 0.30f + variation, 0.88f};
    default:
        return (Color){0};
    }
}
