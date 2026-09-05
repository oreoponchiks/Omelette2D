#include "Sandbox.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define EXPECT(condition, message)                                                                 \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "FAIL: %s (line %d)\n", message, __LINE__);                            \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)

typedef struct Stats {
    int count, min_x, max_x, min_y, max_y;
    double x, y;
} Stats;
static Sandbox* create(void) {
    Sandbox* s = sandbox_create();
    if (!s) {
        fputs("Could not allocate test sandbox\n", stderr);
        exit(2);
    }
    return s;
}
static void run(Sandbox* s, int ticks) {
    for (int i = 0; i < ticks; ++i)
        sandbox_update(s, 1.0f / 60.0f);
}
static void rectangle(Sandbox* s, int left, int top, int right, int bottom, Material material) {
    for (int y = top; y <= bottom; ++y)
        for (int x = left; x <= right; ++x)
            sandbox_paint(s, x, y, material, 0);
}
static void basin(Sandbox* s, int left, int right, int top, int bottom) {
    rectangle(s, left, top, left, bottom, Material_Stone);
    rectangle(s, right, top, right, bottom, Material_Stone);
    rectangle(s, left, bottom, right, bottom, Material_Stone);
}
static Stats stats(Sandbox* s, Material material) {
    Stats result = {0, Sandbox_Width, -1, Sandbox_Height, -1, 0, 0};
    for (int y = 0; y < Sandbox_Height; ++y)
        for (int x = 0; x < Sandbox_Width; ++x)
            if (sandbox_sample(s, x, y).material == material) {
                ++result.count;
                result.x += x;
                result.y += y;
                if (x < result.min_x)
                    result.min_x = x;
                if (x > result.max_x)
                    result.max_x = x;
                if (y < result.min_y)
                    result.min_y = y;
                if (y > result.max_y)
                    result.max_y = y;
            }
    if (result.count) {
        result.x /= result.count;
        result.y /= result.count;
    }
    return result;
}

static void test_fall_and_walls(void) {
    Sandbox* s = create();
    rectangle(s, 0, 100, Sandbox_Width - 1, 100, Material_Stone);
    sandbox_paint(s, 160, 10, Material_Sand, 0);
    run(s, 5);
    Stats early = stats(s, Material_Sand);
    run(s, 5);
    Stats later = stats(s, Material_Sand);
    EXPECT(later.y - early.y > early.y - 10, "falling sand accelerates");
    run(s, 100);
    Stats settled = stats(s, Material_Sand);
    EXPECT(settled.count == 1 && settled.max_y == 99, "fast sand stops above a one-cell floor");
    EXPECT(stats(s, Material_Stone).count == 2 * Sandbox_Width, "collisions preserve walls");
    sandbox_destroy(s);
}
static void test_density(void) {
    Sandbox* s = create();
    basin(s, 100, 160, 60, 140);
    rectangle(s, 101, 100, 159, 139, Material_Water);
    rectangle(s, 123, 85, 128, 90, Material_Sand);
    rectangle(s, 135, 90, 140, 95, Material_Oil);
    run(s, 180);
    Stats sand = stats(s, Material_Sand), water = stats(s, Material_Water),
          oil = stats(s, Material_Oil);
    printf("Density: sand y=%.1f water y=%.1f oil y=%.1f\n", sand.y, water.y, oil.y);
    EXPECT(sand.count == 36 && water.count == 59 * 40 && oil.count == 36,
           "density sorting preserves particles");
    EXPECT(sand.y > water.y && sand.max_y >= 135, "sand sinks through water");
    EXPECT(oil.y < water.y, "oil floats above water");
    EXPECT(stats(s, Material_Mud).count == 0, "sand and water do not instantly become mud");
    sandbox_destroy(s);
}
static int liquid_width(Material material) {
    Sandbox* s = create();
    basin(s, 80, 240, 80, 140);
    rectangle(s, 150, 134, 159, 139, material);
    run(s, 55);
    Stats a = stats(s, material);
    EXPECT(a.count == 60, "lateral flow conserves liquid");
    EXPECT(a.min_x > 80 && a.max_x < 240 && a.max_y < 140, "liquid remains in its container");
    sandbox_destroy(s);
    return a.max_x - a.min_x;
}
static void test_flow_and_gas(void) {
    int water = liquid_width(Material_Water), mud = liquid_width(Material_Mud);
    printf("Spread after 55 ticks: water=%d mud=%d\n", water, mud);
    EXPECT(water > mud, "water spreads faster than viscous mud");
    Sandbox* s = create();
    rectangle(s, 150, 120, 159, 123, Material_Smoke);
    run(s, 70);
    Stats smoke = stats(s, Material_Smoke);
    EXPECT(smoke.count == 40 && smoke.y < 110, "smoke rises and persists");
    EXPECT(smoke.max_x - smoke.min_x > 9, "gas diffuses sideways");
    sandbox_destroy(s);
}
static void test_heat(void) {
    Sandbox* s = create();
    sandbox_paint(s, 50, 100, Material_Water, 0);
    sandbox_heat(s, 50, 100, -35, 0);
    run(s, 1);
    EXPECT(sandbox_sample(s, 50, 100).material == Material_Ice, "cold water freezes");
    sandbox_heat(s, 50, 100, 50, 0);
    run(s, 1);
    EXPECT(sandbox_sample(s, 50, 100).material == Material_Water, "warm ice melts");
    sandbox_heat(s, 50, 100, 100, 0);
    run(s, 1);
    EXPECT(stats(s, Material_Steam).count == 1, "hot water boils into steam");
    Stats steam = stats(s, Material_Steam);
    sandbox_heat(s, steam.min_x, steam.min_y, -70, 0);
    run(s, 1);
    EXPECT(stats(s, Material_Water).count == 1, "cool steam condenses by temperature");
    sandbox_clear(s);
    sandbox_paint(s, 50, 100, Material_Water, 0);
    sandbox_heat(s, 50, 100, 85.5f, 0);
    run(s, 10);
    EXPECT(stats(s, Material_Steam).count == 1,
           "steam does not immediately flicker back at the boiling threshold");
    sandbox_clear(s);
    sandbox_paint(s, 100, 100, Material_Stone, 0);
    sandbox_paint(s, 101, 100, Material_Stone, 0);
    sandbox_heat(s, 100, 100, 500, 0);
    run(s, 10);
    EXPECT(sandbox_sample(s, 101, 100).temperature > 50, "heat conducts into adjacent material");
    EXPECT(sandbox_sample(s, 100, 100).temperature < 520, "heat leaves the hotter material");
    sandbox_clear(s);
    sandbox_paint(s, 100, 100, Material_Lava, 0);
    sandbox_heat(s, 100, 100, -600, 0);
    run(s, 1);
    EXPECT(sandbox_sample(s, 100, 100).material == Material_Stone, "cooled lava solidifies");
    sandbox_heat(s, 100, 100, 700, 0);
    run(s, 1);
    EXPECT(stats(s, Material_Lava).count == 1, "hot stone melts");
    sandbox_destroy(s);
}
static void test_fire_and_reactions(void) {
    Sandbox* s = create();
    sandbox_paint(s, 160, 100, Material_Wood, 0);
    sandbox_heat(s, 160, 100, 400, 0);
    run(s, 70);
    EXPECT(sandbox_sample(s, 160, 100).material == Material_Wood,
           "burning wood stays in place while fuel remains");
    EXPECT(stats(s, Material_Fire).count + stats(s, Material_Smoke).count > 0,
           "burning fuel emits flames");
    run(s, 260);
    EXPECT(stats(s, Material_Wood).count == 0, "fuel eventually burns out");
    sandbox_clear(s);
    sandbox_paint(s, 100, 100, Material_Oil, 0);
    sandbox_heat(s, 100, 100, 300, 0);
    run(s, 1);
    EXPECT(stats(s, Material_Fire).count == 1, "hot oil ignites");
    EXPECT(sandbox_sample(s, 100, 100).pressure > 0.5f, "oil ignition generates pressure");
    sandbox_clear(s);
    sandbox_paint(s, 100, 100, Material_Fire, 0);
    sandbox_paint(s, 101, 100, Material_Water, 0);
    run(s, 1);
    EXPECT(stats(s, Material_Fire).count == 0, "water quenches flames");
    sandbox_clear(s);
    rectangle(s, 100, 100, 110, 110, Material_Water);
    sandbox_paint(s, 105, 105, Material_DilutedAcid, 0);
    run(s, 100);
    EXPECT(stats(s, Material_DilutedAcid).count == 1, "diluted acid cannot multiply through water");
    const int neighbors[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    for (int n = 0; n < 4; ++n) {
        sandbox_clear(s);
        sandbox_paint(s, 100, 100, Material_Acid, 0);
        sandbox_paint(s, 100 + neighbors[n][0], 100 + neighbors[n][1], Material_Water, 0);
        run(s, 1);
        EXPECT(stats(s, Material_DilutedAcid).count == 2,
               "acid mixes with water in every direction");
    }
    sandbox_destroy(s);
}
static void test_air(void) {
    Sandbox* s = create();
    rectangle(s, 160, 0, 163, Sandbox_Height - 1, Material_Stone);
    sandbox_add_pressure(s, 152, 80, 10);
    run(s, 10);
    EXPECT(fabsf(sandbox_sample(s, 148, 80).pressure) > 0.001f,
           "pressure spreads to adjacent air tiles");
    run(s, 70);
    EXPECT(fabsf(sandbox_sample(s, 168, 80).pressure) < 0.00001f,
           "sealed solid wall blocks pressure");
    EXPECT(fabsf(sandbox_sample(s, 152, 80).pressure) < 10, "pressure pulse dissipates");
    sandbox_clear(s);
    EXPECT(sandbox_sample(s, 152, 80).pressure == 0, "clear removes residual air motion");
    sandbox_destroy(s);

    Sandbox* windy = create();
    Sandbox* still = create();
    sandbox_paint(windy, 160, 100, Material_Smoke, 2);
    sandbox_paint(still, 160, 100, Material_Smoke, 2);
    for (int n = 0; n < 40; ++n) {
        sandbox_add_pressure(windy, 148, 100, 2);
        run(windy, 1);
        run(still, 1);
    }
    Stats wind_smoke = stats(windy, Material_Smoke), still_smoke = stats(still, Material_Smoke);
    EXPECT(wind_smoke.x > still_smoke.x + 1, "pressure-driven air advects smoke");
    sandbox_destroy(windy);
    sandbox_destroy(still);
}
static void test_determinism_and_views(void) {
    Sandbox* a = create();
    Sandbox* b = create();
    for (int m = 1; m < Material_Count; ++m) {
        sandbox_paint(a, 12 + (m % 16) * 19, 20 + (m / 16) * 30, (Material)m, 3);
        sandbox_paint(b, 12 + (m % 16) * 19, 20 + (m / 16) * 30, (Material)m, 3);
    }
    for (int n = 0; n < 210; ++n) {
        sandbox_update(a, 1.0f / 60.0f);
        sandbox_update(b, 1.0f / 120.0f);
        sandbox_update(b, 1.0f / 120.0f);
    }
    bool equal = true, finite = true;
    for (int y = 0; y < Sandbox_Height; ++y)
        for (int x = 0; x < Sandbox_Width; ++x) {
            SandboxSample sa = sandbox_sample(a, x, y), sb = sandbox_sample(b, x, y);
            if (sa.material != sb.material || sa.temperature != sb.temperature ||
                sa.pressure != sb.pressure || sa.velocity_x != sb.velocity_x ||
                sa.velocity_y != sb.velocity_y)
                equal = false;
            if (!isfinite(sa.temperature) || !isfinite(sa.pressure) || !isfinite(sa.velocity_x) ||
                !isfinite(sa.velocity_y) || (unsigned)sa.material >= Material_Count)
                finite = false;
        }
    EXPECT(equal, "fixed-step physics is independent of rendering frame rate");
    EXPECT(finite, "mixed-material state remains finite and valid");
    for (int view = SandboxView_Normal; view <= SandboxView_Pressure; ++view) {
        sandbox_set_view(a, (SandboxView)view);
        size_t count;
        const Sprite* sprites = sandbox_build_sprites(a, 1280, 720, &count);
        EXPECT(count > 0 && count < 100000, "inspection view fits renderer capacity");
        bool valid = true;
        for (size_t i = 0; i < count; ++i)
            if (!isfinite(sprites[i].color.r) || !isfinite(sprites[i].color.a) ||
                sprites[i].width <= 0)
                valid = false;
        EXPECT(valid, "inspection view emits finite sprites");
    }
    sandbox_clear(a);
    sandbox_update(a, NAN);
    sandbox_update(a, -1);
    sandbox_update(a, INFINITY);
    EXPECT(stats(a, Material_Stone).count == Sandbox_Width,
           "invalid time steps do not alter the world");
    sandbox_destroy(a);
    sandbox_destroy(b);
}
static void test_material_catalog(void) {
    Sandbox* s = create();
    EXPECT(Material_Count == 65, "exactly fifty materials added to the original fifteen entries");
    for (int m = 0; m < Material_Count; ++m) {
        const char* name = sandbox_material_name((Material)m);
        EXPECT(name[0] && strcmp(name, "Unknown"), "every material has a name");
        EXPECT(sandbox_material_description((Material)m)[0], "every material has a behavior hint");
        EXPECT(sandbox_material_category((Material)m) < MaterialCategory_Count,
               "every material has a valid category");
        for (int other = 0; other < m; ++other)
            EXPECT(strcmp(name, sandbox_material_name((Material)other)) != 0,
                   "material names are unique");
        sandbox_clear(s);
        sandbox_paint(s, 100, 100, (Material)m, 0);
        EXPECT(sandbox_sample(s, 100, 100).material == m, "every catalog material is paintable");
        size_t count;
        const Sprite* sprites = sandbox_build_sprites(s, 320, 180, &count);
        EXPECT(count == (size_t)(Sandbox_Width + (m != 0)), "all materials have visible sprites");
        if (m)
            EXPECT(sprites[0].color.a > 0, "new material is not transparent by default");
    }
    EXPECT(strcmp(sandbox_material_name((Material)-1), "Unknown") == 0,
           "invalid catalog lookup is safe");
    sandbox_destroy(s);
}
static void test_new_reactions(void) {
    Sandbox* s = create();
    const struct {
        Material from, to;
        float temperature;
    } transitions[] = {{Material_Snow, Material_Water, 20},
                       {Material_Gravel, Material_Lava, 1200},
                       {Material_Clay, Material_Brick, 800},
                       {Material_Rust, Material_MoltenMetal, 1400},
                       {Material_MetalDust, Material_MoltenMetal, 1400},
                       {Material_Metal, Material_MoltenMetal, 1400},
                       {Material_Copper, Material_MoltenMetal, 1200},
                       {Material_Steel, Material_MoltenMetal, 1600},
                       {Material_Glass, Material_MoltenGlass, 1500},
                       {Material_GlassShards, Material_MoltenGlass, 1500},
                       {Material_MoltenMetal, Material_Metal, 1000},
                       {Material_MoltenGlass, Material_Glass, 1100},
                       {Material_Wax, Material_LiquidWax, 90},
                       {Material_LiquidWax, Material_Wax, 20},
                       {Material_Brick, Material_Lava, 1800},
                       {Material_Concrete, Material_Lava, 1800},
                       {Material_Obsidian, Material_Lava, 1700},
                       {Material_LiquidNitrogen, Material_Helium, -150},
                       {Material_DryIce, Material_CarbonDioxide, -50},
                       {Material_Honey, Material_Coal, 220},
                       {Material_Alcohol, Material_Fire, 220},
                       {Material_Gasoline, Material_Fire, 180},
                       {Material_LiquidWax, Material_Fire, 400}};
    for (size_t n = 0; n < sizeof(transitions) / sizeof(transitions[0]); ++n) {
        sandbox_clear(s);
        sandbox_paint(s, 100, 100, transitions[n].from, 0);
        sandbox_heat(s, 100, 100,
                     transitions[n].temperature - sandbox_sample(s, 100, 100).temperature, 0);
        run(s, 1);
        if (stats(s, transitions[n].to).count != 1) {
            fprintf(stderr, "Transition failed: %s -> %s\n",
                    sandbox_material_name(transitions[n].from),
                    sandbox_material_name(transitions[n].to));
            ++failures;
        }
    }
    const Material solutes[] = {Material_Salt, Material_Sugar, Material_Honey};
    const int neighbors[4][2] = {{0, -1}, {1, 0}, {-1, 0}, {0, 1}};
    for (int m = 0; m < 3; ++m)
        for (int n = 0; n < 4; ++n) {
            sandbox_clear(s);
            sandbox_paint(s, 100, 100, solutes[m], 0);
            sandbox_paint(s, 100 + neighbors[n][0], 100 + neighbors[n][1], Material_Water, 0);
            run(s, 1);
            EXPECT(stats(s, m == 0 ? Material_SaltWater : Material_SugarWater).count == 2,
                   "solutes dissolve in all four directions");
        }
    for (int m = 0; m < 2; ++m) {
        sandbox_clear(s);
        sandbox_paint(s, 100, 100, m ? Material_SugarWater : Material_SaltWater, 0);
        sandbox_heat(s, 100, 100, 120, 0);
        run(s, 1);
        EXPECT(stats(s, m ? Material_Sugar : Material_Salt).count == 1,
               "boiling recovers dissolved solids");
        EXPECT(stats(s, Material_Steam).count == 1, "boiling solutions emits steam");
    }
    sandbox_clear(s);
    sandbox_paint(s, 100, 100, Material_Seed, 0);
    sandbox_paint(s, 100, 99, Material_Water, 0);
    sandbox_paint(s, 100, 101, Material_Soil, 0);
    run(s, 1);
    EXPECT(stats(s, Material_Plant).count == 1 && stats(s, Material_Seed).count == 0,
           "wet seeds germinate beside soil");
    sandbox_clear(s);
    sandbox_paint(s, 100, 100, Material_Fire, 0);
    sandbox_paint(s, 101, 100, Material_CarbonDioxide, 0);
    run(s, 1);
    EXPECT(stats(s, Material_Fire).count == 0, "carbon dioxide extinguishes fire");
    sandbox_clear(s);
    sandbox_paint(s, 100, 100, Material_Oxygen, 0);
    sandbox_paint(s, 101, 100, Material_Fire, 0);
    run(s, 1);
    EXPECT(stats(s, Material_Oxygen).count == 0 && stats(s, Material_Fire).count == 1,
           "oxygen is consumed supporting fire");
    sandbox_clear(s);
    sandbox_paint(s, 100, 100, Material_Glass, 0);
    sandbox_add_pressure(s, 104, 100, 15);
    run(s, 1);
    EXPECT(stats(s, Material_GlassShards).count == 1, "air pressure shatters glass");
    sandbox_clear(s);
    rectangle(s, 100, 100, 112, 100, Material_Fuse);
    sandbox_paint(s, 113, 100, Material_TNT, 0);
    sandbox_heat(s, 100, 100, 500, 0);
    run(s, 100);
    EXPECT(stats(s, Material_TNT).count == 0, "burning fuse triggers an attached explosive");
    sandbox_destroy(s);
}
static void test_bombs(void) {
    Sandbox* s = create();
    sandbox_paint(s, 160, 100, Material_RemoteBomb, 0);
    run(s, 200);
    EXPECT(stats(s, Material_RemoteBomb).count == 1,
           "remote charge waits indefinitely for a trigger");
    sandbox_paint(s, 165, 100, Material_Glass, 0);
    sandbox_paint(s, 166, 105, Material_Steel, 0);
    sandbox_detonate_remote(s);
    EXPECT(stats(s, Material_RemoteBomb).count == 1, "arming does not advance a paused simulation");
    run(s, 1);
    EXPECT(stats(s, Material_RemoteBomb).count == 0 && stats(s, Material_Fire).count > 0,
           "remote detonation creates fire");
    EXPECT(stats(s, Material_Glass).count == 0 && stats(s, Material_GlassShards).count > 0,
           "blast breaks glass into debris");
    EXPECT(stats(s, Material_Steel).count == 1, "steel withstands weaker edge of blast");
    EXPECT(stats(s, Material_GlassShards).x > 165, "blast throws debris outward");
    EXPECT(sandbox_sample(s, 160, 100).pressure > 1, "bomb generates a pressure wave");
    sandbox_clear(s);
    sandbox_paint(s, 100, 100, Material_RemoteBomb, 0);
    sandbox_paint(s, 112, 100, Material_TNT, 0);
    sandbox_paint(s, 124, 100, Material_TNT, 0);
    sandbox_detonate_remote(s);
    run(s, 1);
    EXPECT(stats(s, Material_TNT).count == 2, "chain charges wait until the next tick");
    run(s, 2);
    EXPECT(stats(s, Material_TNT).count == 0,
           "blast chains to unarmed charges beyond the original radius");
    sandbox_clear(s);
    sandbox_paint(s, 160, 100, Material_TimedBomb, 0);
    run(s, 179);
    EXPECT(stats(s, Material_TimedBomb).count == 1, "timer does not fire before three seconds");
    run(s, 1);
    EXPECT(stats(s, Material_TimedBomb).count == 0 && stats(s, Material_Fire).count > 0,
           "timer detonates at three seconds");
    sandbox_clear(s);
    rectangle(s, 0, 100, 319, 100, Material_Steel);
    sandbox_paint(s, 160, 20, Material_ImpactBomb, 0);
    run(s, 5);
    EXPECT(stats(s, Material_ImpactBomb).count == 1, "impact bomb survives free fall");
    run(s, 60);
    EXPECT(stats(s, Material_ImpactBomb).count == 0, "impact bomb detonates on landing");
    sandbox_clear(s);
    sandbox_paint(s, 160, 178, Material_FireBomb, 0);
    run(s, 180);
    EXPECT(stats(s, Material_FireBomb).count == 0 && stats(s, Material_Fire).count > 20,
           "fire bomb releases a flame cloud");
    sandbox_clear(s);
    sandbox_paint(s, 160, 178, Material_CryoBomb, 0);
    run(s, 179);
    sandbox_paint(s, 165, 177, Material_Water, 1);
    run(s, 2);
    EXPECT(stats(s, Material_CryoBomb).count == 0 && stats(s, Material_Ice).count > 0,
           "cryo blast freezes surrounding water");
    EXPECT(stats(s, Material_Fire).count == 0, "cryo blast does not create flames");
    const Material ignited[] = {Material_Gunpowder, Material_TNT, Material_Hydrogen,
                                Material_NaturalGas};
    for (int n = 0; n < 4; ++n) {
        sandbox_clear(s);
        sandbox_paint(s, 160, 100, ignited[n], 0);
        sandbox_heat(s, 160, 100, 500, 0);
        run(s, 1);
        EXPECT(stats(s, ignited[n]).count == 0 && sandbox_sample(s, 160, 100).pressure > 0,
               "heat triggers explosive materials");
    }
    sandbox_clear(s);
    rectangle(s, 0, 0, 39, 19, Material_RemoteBomb);
    sandbox_detonate_remote(s);
    run(s, 1);
    EXPECT(stats(s, Material_RemoteBomb).count > 0,
           "large blasts defer excess charges instead of recursing");
    run(s, 20);
    EXPECT(stats(s, Material_RemoteBomb).count == 0,
           "deferred chain finishes, including charges at world edges");
    sandbox_paint(s, 100, 100, Material_RemoteBomb, 0);
    sandbox_detonate_remote(s);
    sandbox_clear(s);
    run(s, 1);
    EXPECT(stats(s, Material_Fire).count == 0, "clear cancels armed bombs");
    sandbox_destroy(s);
}
int main(void) {
    test_fall_and_walls();
    test_density();
    test_flow_and_gas();
    test_heat();
    test_fire_and_reactions();
    test_air();
    test_material_catalog();
    test_new_reactions();
    test_bombs();
    test_determinism_and_views();
    if (!failures)
        puts("All physics behavior tests passed.");
    return failures ? 1 : 0;
}
