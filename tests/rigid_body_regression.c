#include "Sandbox.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int failures;
#define CHECK(c, message)                                                                          \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "FAIL: %s (line %d)\n", message, __LINE__);                            \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)
static Sandbox* create(void) {
    Sandbox* s = sandbox_create();
    if (!s)
        exit(2);
    return s;
}
static void run(Sandbox* s, int ticks) {
    for (int n = 0; n < ticks; ++n)
        sandbox_update(s, 1.0f / 60.0f);
}
static int countMaterial(Sandbox* s, Material m) {
    int count = 0;
    for (int y = 0; y < Sandbox_Height; ++y)
        for (int x = 0; x < Sandbox_Width; ++x)
            count += sandbox_sample(s, x, y).material == m;
    return count;
}
static SandboxBodyInfo info(Sandbox* s, uint32_t id) {
    SandboxBodyInfo b = {0};
    CHECK(sandbox_body_info(s, id, &b), "body handle is valid");
    return b;
}
static SandboxShape preset(ShapePreset p, Material m) {
    SandboxShape shape;
    sandbox_shape_preset(&shape, p, m);
    return shape;
}
static void rectangle(Sandbox* s, int l, int t, int r, int b, Material m) {
    for (int y = t; y <= b; ++y)
        for (int x = l; x <= r; ++x)
            sandbox_paint(s, x, y, m, 0);
}
static void test_shapes_and_placement(void) {
    Sandbox* s = create();
    for (int p = ShapePreset_Box; p < ShapePreset_Count; ++p) {
        SandboxShape shape = preset((ShapePreset)p, Material_Wood);
        CHECK(sandbox_can_place_shape(s, &shape, 100, 50, 0),
              "all presets are connected and placeable");
        uint32_t id = sandbox_create_body(s, &shape, 100, 50, 0, false);
        CHECK(id && info(s, id).pixel_count > 0, "preset creates a real body");
        CHECK(!sandbox_create_body(s, &shape, 100, 50, 0, false),
              "overlapping placement is rejected");
        CHECK(sandbox_body_count(s) == 1, "failed placement is transactional");
        sandbox_clear(s);
        CHECK(!sandbox_body_info(s, id, &(SandboxBodyInfo){0}), "clear invalidates object handles");
    }
    SandboxShape shape = {0};
    CHECK(!sandbox_create_body(s, &shape, 100, 50, 0, false), "empty shape is rejected");
    sandbox_shape_paint(&shape, 20, 20, Material_Wood, 0);
    sandbox_shape_paint(&shape, 25, 25, Material_Wood, 0);
    CHECK(!sandbox_create_body(s, &shape, 100, 50, 0, false),
          "disconnected custom mask is rejected");
    shape = preset(ShapePreset_Box, Material_Wood);
    CHECK(!sandbox_create_body(s, &shape, 0, 0, 0, false), "out-of-world placement is rejected");
    CHECK(!sandbox_create_body(s, &shape, NAN, 30, 0, false), "nonfinite placement is rejected");
    shape.pixels[0] = Material_Water;
    CHECK(!sandbox_create_body(s, &shape, 100, 50, 0, false),
          "liquids cannot be welded into rigid objects");
    shape = preset(ShapePreset_Box, Material_Wood);
    uint32_t id = sandbox_create_body(s, &shape, 100, 50, 0, true);
    SandboxBodyInfo a = info(s, id);
    run(s, 100);
    SandboxBodyInfo b = info(s, id);
    CHECK(a.x == b.x && a.y == b.y && a.angle == b.angle,
          "fixed objects do not drift under gravity");
    sandbox_destroy(s);
}
static void test_fall_walls_rotation(void) {
    Sandbox* s = create();
    SandboxShape box = preset(ShapePreset_Box, Material_Wood);
    uint32_t id = sandbox_create_body(s, &box, 100, 30, 0, false);
    run(s, 100);
    SandboxBodyInfo b = info(s, id);
    printf("Resting box: y=%.3f vy=%.3f angle=%.3f\n", b.y, b.velocity_y, b.angle);
    CHECK(b.y > 165 && b.y < 175, "loose body falls and lands on the floor");
    CHECK(fabsf(b.velocity_y) < 0.6f, "contact impulses settle a resting object");
    CHECK(b.pixel_count == 288, "undamaged body retains its canonical pixels");
    for (int x = 0; x < Sandbox_Width; ++x)
        CHECK(!sandbox_body_at(s, x, 179), "body never overwrites floor");
    sandbox_clear(s);
    rectangle(s, 180, 0, 180, 178, Material_Steel);
    id = sandbox_create_body(s, &box, 140, 80, 0, false);
    b = info(s, id);
    sandbox_body_impulse(s, id, b.x, b.y, 50000, 0);
    run(s, 30);
    b = info(s, id);
    CHECK(b.x < 180, "fast body cannot tunnel through a one-cell wall");
    for (int y = 0; y < Sandbox_Height; ++y)
        for (int x = 180; x < Sandbox_Width; ++x)
            CHECK(sandbox_body_at(s, x, y) != id,
                  "all body pixels remain on the near side of wall");
    sandbox_clear(s);
    id = sandbox_create_body(s, &box, 140, 70, 0, false);
    b = info(s, id);
    sandbox_body_impulse(s, id, b.x, b.y - 5, 300, 0);
    run(s, 12);
    b = info(s, id);
    CHECK(fabsf(b.angle) > 0.05f, "off-center force rotates an object");
    CHECK(b.pixel_count == 288, "rotation does not destroy canonical shape pixels");
    sandbox_destroy(s);
}
static void test_object_collisions_and_grab(void) {
    Sandbox* s = create();
    SandboxShape box = preset(ShapePreset_Box, Material_Wood);
    uint32_t lower = sandbox_create_body(s, &box, 160, 165, 0, false);
    uint32_t upper = sandbox_create_body(s, &box, 160, 110, 0, false);
    run(s, 140);
    SandboxBodyInfo a = info(s, lower), b = info(s, upper);
    printf("Stack: lower %.2f upper %.2f\n", a.y, b.y);
    CHECK(a.y > b.y + 9 && b.y > 145, "dynamic boxes stack without interpenetrating");
    sandbox_clear(s);
    uint32_t id = sandbox_create_body(s, &box, 100, 70, 0, false);
    a = info(s, id);
    sandbox_body_grab(s, id, a.x, a.y);
    sandbox_body_grab(s, id, a.x + 30, a.y - 10);
    run(s, 25);
    b = info(s, id);
    CHECK(b.x > a.x + 15, "grab spring moves an object toward the target");
    sandbox_body_grab(s, 0, 0, 0);
    run(s, 20);
    a = info(s, id);
    CHECK(a.y > b.y, "released object resumes falling");
    sandbox_destroy(s);
}
static void test_particles_and_water(void) {
    Sandbox* s = create();
    SandboxShape box = preset(ShapePreset_Box, Material_Wood);
    uint32_t id = sandbox_create_body(s, &box, 160, 110, 0, true);
    sandbox_paint(s, 160, 70, Material_Sand, 3);
    int grains = countMaterial(s, Material_Sand);
    run(s, 70);
    CHECK(countMaterial(s, Material_Sand) == grains,
          "particles colliding with objects are conserved");
    int above = 0;
    for (int y = 90; y < 110; ++y)
        for (int x = 145; x < 175; ++x)
            above += sandbox_sample(s, x, y).material == Material_Sand;
    CHECK(above > 0 && sandbox_body_count(s) == 1, "sand rests on an object's collision surface");
    sandbox_clear(s);
    rectangle(s, 80, 80, 80, 178, Material_Steel);
    rectangle(s, 240, 80, 240, 178, Material_Steel);
    rectangle(s, 81, 140, 239, 178, Material_Water);
    int water = countMaterial(s, Material_Water);
    id = sandbox_create_body(s, &box, 140, 90, 0, false);
    SandboxShape rock = preset(ShapePreset_Boulder, Material_Stone);
    uint32_t stone = sandbox_create_body(s, &rock, 190, 90, 0, false);
    run(s, 180);
    SandboxBodyInfo wood = info(s, id), boulder = info(s, stone);
    printf("Water: wood y=%.2f rock y=%.2f particles=%d/%d\n", wood.y, boulder.y,
           countMaterial(s, Material_Water), water);
    CHECK(countMaterial(s, Material_Water) == water,
          "moving bodies displace water without deleting it");
    CHECK(wood.y < boulder.y - 8, "wood floats higher than a dense boulder");
    sandbox_destroy(s);
}
static void test_damage_and_explosions(void) {
    Sandbox* s = create();
    SandboxShape box = preset(ShapePreset_Box, Material_Wood);
    uint32_t id = sandbox_create_body(s, &box, 160, 70, 0, false);
    for (int y = 60; y <= 80; ++y)
        sandbox_paint(s, 160, y, Material_Empty, 0);
    run(s, 1);
    CHECK(sandbox_body_count(s) == 2, "cutting across a body creates independent fragments");
    CHECK(info(s, id).pixel_count < 288, "cut removes pixels from the original body");
    run(s, 100);
    CHECK(sandbox_body_count(s) == 2, "fragments remain stable after collision with the floor");
    sandbox_clear(s);
    SandboxShape tree = preset(ShapePreset_Tree, Material_Wood);
    id = sandbox_create_body(s, &tree, 160, 90, 0, true);
    sandbox_heat(s, 160, 90, 600, 40);
    run(s, 330);
    CHECK(!sandbox_body_info(s, id, &(SandboxBodyInfo){0}) || info(s, id).pixel_count < 100,
          "tree pixels burn and leave the rigid mask");
    sandbox_clear(s);
    SandboxShape steel = preset(ShapePreset_Box, Material_Steel);
    id = sandbox_create_body(s, &steel, 160, 80, 0, false);
    sandbox_paint(s, 140, 76, Material_RemoteBomb, 0);
    sandbox_detonate_remote(s);
    run(s, 1);
    SandboxBodyInfo b = info(s, id);
    CHECK(b.velocity_x > 0, "nearby explosion pushes a surviving rigid object");
    CHECK(fabsf(b.angular_velocity) > 0.0001f, "off-center blast imparts angular momentum");
    sandbox_destroy(s);
}
static void test_limits_and_determinism(void) {
    Sandbox* a = create();
    Sandbox* b = create();
    SandboxShape tiny = {0};
    sandbox_shape_paint(&tiny, 24, 24, Material_Steel, 0);
    for (int n = 0; n < Sandbox_MaxBodies; ++n)
        CHECK(sandbox_create_body(a, &tiny, 20.0f + (n % 32) * 8, 20.0f + (n / 32) * 8, 0, false) !=
                  0,
              "object slots are usable");
    CHECK(Sandbox_MaxBodies == 256 && sandbox_body_at(a, 268, 76) != 0,
          "slot 256 has a valid grid owner without byte overflow");
    run(a, 1);
    CHECK(sandbox_body_count(a) == Sandbox_MaxBodies, "all 256 objects survive a physics step");
    CHECK(!sandbox_create_body(a, &tiny, 20, 50, 0, false), "object capacity fails cleanly");
    sandbox_clear(a);
    SandboxShape tree = preset(ShapePreset_Tree, Material_Wood),
                 rock = preset(ShapePreset_Boulder, Material_Stone);
    uint32_t ia = sandbox_create_body(a, &tree, 140, 60, 0.2f, false);
    uint32_t ib = sandbox_create_body(b, &tree, 140, 60, 0.2f, false);
    sandbox_create_body(a, &rock, 160, 110, 0, false);
    sandbox_create_body(b, &rock, 160, 110, 0, false);
    for (int n = 0; n < 120; ++n) {
        sandbox_update(a, 1.0f / 60.0f);
        sandbox_update(b, 1.0f / 120.0f);
        sandbox_update(b, 1.0f / 120.0f);
    }
    SandboxBodyInfo sa = info(a, ia), sb = info(b, ib);
    CHECK(sa.x == sb.x && sa.y == sb.y && sa.angle == sb.angle,
          "rigid bodies retain fixed-step determinism");
    bool valid = true;
    for (int y = 0; y < Sandbox_Height; ++y)
        for (int x = 0; x < Sandbox_Width; ++x) {
            SandboxSample p = sandbox_sample(a, x, y), q = sandbox_sample(b, x, y);
            if (p.material != q.material || !isfinite(p.temperature) || !isfinite(p.velocity_x))
                valid = false;
        }
    CHECK(valid, "mixed object/particle state stays finite and deterministic");
    sandbox_destroy(a);
    sandbox_destroy(b);
}
static void test_cups_melting_and_demo(void) {
    Sandbox* s = create();
    SandboxShape cup = {0};
    for (int y = 12; y <= 32; ++y) {
        sandbox_shape_paint(&cup, 12, y, Material_Glass, 0);
        sandbox_shape_paint(&cup, 34, y, Material_Glass, 0);
    }
    for (int x = 12; x <= 34; ++x)
        sandbox_shape_paint(&cup, x, 32, Material_Glass, 0);
    uint32_t id = sandbox_create_body(s, &cup, 160, 90, 0, true);
    CHECK(id != 0, "concave cup shape is placeable");
    CHECK(sandbox_body_at(s, 160, 90) == 0, "cup interior remains hollow");
    CHECK(countMaterial(s, Material_Glass) == 63,
          "half-cell rasterization retains the one-pixel cup floor");
    sandbox_paint(s, 160, 80, Material_Water, 2);
    int water = countMaterial(s, Material_Water);
    run(s, 80);
    CHECK(countMaterial(s, Material_Water) == water, "hollow objects preserve contained liquid");
    int contained = 0;
    for (int y = 78; y <= 98; ++y)
        for (int x = 149; x <= 170; ++x)
            contained += sandbox_sample(s, x, y).material == Material_Water;
    CHECK(contained == water, "cup walls contain water using actual mask geometry");
    sandbox_clear(s);
    SandboxShape ice = preset(ShapePreset_Box, Material_Ice);
    id = sandbox_create_body(s, &ice, 160, 70, 0, true);
    sandbox_heat(s, 160, 70, 100, 30);
    run(s, 1);
    CHECK(!sandbox_body_info(s, id, &(SandboxBodyInfo){0}),
          "melted ice object releases its rigid body slot");
    CHECK(countMaterial(s, Material_Water) == 288,
          "melting returns object pixels to the fluid simulation");
    sandbox_load_object_demo(s);
    CHECK(sandbox_body_count(s) == 3, "playground creates a tree, boulder, and floating box");
    water = countMaterial(s, Material_Water);
    int vegetation = countMaterial(s, Material_Plant);
    run(s, 240);
    CHECK(sandbox_body_count(s) == 3 &&
              countMaterial(s, Material_Water) + countMaterial(s, Material_Plant) ==
                  water + vegetation,
          "playground conserves water including water consumed by plant growth");
    sandbox_destroy(s);
}
static void test_sliding_and_variations(void) {
    Sandbox* s = create();
    SandboxShape box = preset(ShapePreset_Box, Material_Wood);
    for (int side = -1; side <= 1; side += 2) {
        sandbox_clear(s);
        uint32_t id = sandbox_create_body(s, &box, side < 0 ? 28.0f : 292.0f, 40, 0.3f, false);
        SandboxBodyInfo b = info(s, id);
        sandbox_body_impulse(s, id, b.x, b.y - 3, side * 50000.0f, 0);
        run(s, 100);
        b = info(s, id);
        printf("Edge %d: x %.2f y %.2f\n", side, b.x, b.y);
        CHECK(b.y > 150, "objects slide down screen edges instead of hanging by a corner");
    }
    sandbox_clear(s);
    SandboxShape pillar = {0};
    for (int y = 1; y < 47; ++y)
        for (int x = 22; x <= 25; ++x)
            sandbox_shape_paint(&pillar, x, y, Material_Steel, 0);
    sandbox_create_body(s, &pillar, 160, 90, 0, true);
    SandboxShape rock = preset(ShapePreset_Boulder, Material_Stone);
    rock.break_speed = 0; /* Exercise contact sliding independently of throw damage. */
    uint32_t id = sandbox_create_body(s, &rock, 130, 65, 0, false);
    SandboxBodyInfo b = info(s, id);
    sandbox_body_impulse(s, id, b.x, b.y - 4, 50000, 0);
    run(s, 100);
    b = info(s, id);
    CHECK(b.y > 150, "boulder slides past the side of another object");
    sandbox_clear(s);
    sandbox_create_body(s, &box, 160, 120, 0, true);
    id = sandbox_create_body(s, &rock, 184, 80, 0, false);
    run(s, 180);
    b = info(s, id);
    CHECK(b.y > 150,
          "overhanging boulder falls off an object edge rather than locking to a pixel corner");
    sandbox_clear(s);
    SandboxShape previous = {0}, shape;
    for (int p = ShapePreset_Tree; p < ShapePreset_Count; ++p) {
        if (!sandbox_shape_varies((ShapePreset)p))
            continue;
        for (uint32_t seed = 1; seed <= 32; ++seed) {
            sandbox_shape_generate(&shape, (ShapePreset)p, Material_Wood, seed * 7919);
            CHECK(sandbox_can_place_shape(s, &shape, 160, 70, 0),
                  "generated natural shapes remain connected and placeable");
            SandboxShape repeat;
            sandbox_shape_generate(&repeat, (ShapePreset)p, Material_Wood, seed * 7919);
            CHECK(memcmp(&shape, &repeat, sizeof(shape)) == 0,
                  "same seed reproduces geometry and shading");
            if (seed > 1)
                CHECK(memcmp(shape.pixels, previous.pixels, sizeof(shape.pixels)) != 0,
                      "different seeds generate different silhouettes");
            previous = shape;
        }
    }
    sandbox_destroy(s);
}
static void test_thrown_shattering(void) {
    Sandbox* s = create();
    const ShapePreset types[] = {ShapePreset_Boulder, ShapePreset_Tree, ShapePreset_Pine};
    for (int n = 0; n < 3; ++n) {
        sandbox_clear(s);
        SandboxShape shape = preset(types[n], Material_Wood);
        rectangle(s, 240, 0, 240, 178, Material_Steel);
        uint32_t id = sandbox_create_body(s, &shape, 175, 75, 0, false);
        SandboxBodyInfo before = info(s, id);
        sandbox_body_impulse(s, id, before.x, before.y, 100000, 0);
        run(s, 1);
        CHECK(info(s, id).pixel_count == before.pixel_count,
              "throw does not fracture an object while it is airborne");
        run(s, 20);
        SandboxBodyInfo after = {0};
        bool remains = sandbox_body_info(s, id, &after);
        CHECK(!remains || after.pixel_count < before.pixel_count,
              "hard thrown impact fractures the original body");
        CHECK(sandbox_body_count(s) > 1, "thrown natural objects split into independent chunks");
        CHECK(countMaterial(s, n == 0 ? Material_Gravel : Material_Sawdust) > 0,
              "impact produces material-appropriate debris");
        if (n)
            CHECK(countMaterial(s, Material_Dust) > 0, "shattered trees shed leafy particles");
        run(s, 100);
        CHECK(sandbox_body_count(s) <= Sandbox_MaxBodies,
              "settling fragments stay within the object limit");
    }
    sandbox_clear(s);
    SandboxShape rock = preset(ShapePreset_Boulder, Material_Stone);
    uint32_t id = sandbox_create_body(s, &rock, 160, 160, 0, false);
    int pixels = info(s, id).pixel_count;
    sandbox_body_impulse(s, id, 160, 160, 0, 500);
    run(s, 30);
    CHECK(info(s, id).pixel_count == pixels, "gentle drop does not shatter a boulder");
    sandbox_clear(s);
    rectangle(s, 240, 0, 240, 178, Material_Steel);
    id = sandbox_create_body(s, &rock, 120, 75, 0, false);
    SandboxBodyInfo b = info(s, id);
    sandbox_body_grab(s, id, b.x, b.y);
    sandbox_body_grab(s, id, 230, b.y);
    run(s, 4);
    sandbox_body_grab(s, 0, 0, 0);
    run(s, 30);
    CHECK(sandbox_body_count(s) > 1 && countMaterial(s, Material_Gravel) > 0,
          "releasing a fast grab arms impact shattering");
    sandbox_clear(s);
    id = sandbox_create_body(s, &rock, 160, 160, 0, true);
    sandbox_body_impulse(s, id, 160, 160, 100000, 0);
    run(s, 10);
    CHECK(info(s, id).pixel_count == pixels, "fixed scenery does not arm from throw impulses");
    sandbox_clear(s);
    id = sandbox_create_body(s, &rock, 280, 80, 0, false);
    b = info(s, id);
    sandbox_body_impulse(s, id, b.x, b.y, 100000, 0);
    run(s, 20);
    CHECK(sandbox_body_count(s) > 1 && countMaterial(s, Material_Gravel) > 0,
          "screen boundary is a valid shattering impact");
    sandbox_clear(s);
    SandboxShape tiny = {0};
    sandbox_shape_paint(&tiny, 24, 24, Material_Steel, 0);
    for (int n = 0; n < Sandbox_MaxBodies - 1; ++n)
        sandbox_create_body(s, &tiny, 10.0f + (n % 32) * 5, 10.0f + (n / 32) * 5, 0, true);
    rectangle(s, 240, 0, 240, 178, Material_Steel);
    id = sandbox_create_body(s, &rock, 190, 80, 0, false);
    b = info(s, id);
    sandbox_body_impulse(s, id, b.x, b.y, 100000, 0);
    run(s, 20);
    CHECK(sandbox_body_count(s) <= Sandbox_MaxBodies && countMaterial(s, Material_Gravel) > 0,
          "full object pool releases excess impact fragments as debris");
    sandbox_destroy(s);
}
static int gallery(const char* path) {
    SandboxShape shapes[12];
    const ShapePreset presets[] = {ShapePreset_Tree,   ShapePreset_Pine,   ShapePreset_Birch,
                                   ShapePreset_Willow, ShapePreset_Palm,   ShapePreset_DeadTree,
                                   ShapePreset_Crate,  ShapePreset_Barrel, ShapePreset_Bridge,
                                   ShapePreset_Arch,   ShapePreset_House,  ShapePreset_Tower};
    for (int n = 0; n < 12; ++n)
        sandbox_shape_generate(&shapes[n], presets[n], Material_Wood, 7919 * (uint32_t)(n + 1));
    FILE* file = NULL;
#ifdef _MSC_VER
    if (fopen_s(&file, path, "wb") != 0)
        return 1;
#else
    file = fopen(path, "wb");
    if (!file)
        return 1;
#endif
    fprintf(file, "P6\n864 288\n255\n");
    for (int y = 0; y < 288; ++y)
        for (int x = 0; x < 864; ++x) {
            int sx = x / 3, sy = y / 3, index = (sy / 48) * 6 + sx / 48,
                local = (sy % 48) * 48 + sx % 48;
            unsigned color = shapes[index].pixels[local] ? shapes[index].colors[local] : 0x18202A;
            unsigned char rgb[3] = {(unsigned char)(color >> 16), (unsigned char)(color >> 8),
                                    (unsigned char)color};
            fwrite(rgb, 1, 3, file);
        }
    return fclose(file) != 0;
}
static int benchmark(int count) {
    Sandbox* s = create();
    SandboxShape shape = {0};
    for (int y = 21; y < 27; ++y)
        for (int x = 21; x < 27; ++x)
            sandbox_shape_paint(&shape, x, y, Material_Wood, 0);
    if (count < 1 || count > Sandbox_MaxBodies) {
        sandbox_destroy(s);
        return 1;
    }
    uint32_t ids[Sandbox_MaxBodies];
    for (int n = 0; n < count; ++n) {
        ids[n] =
            sandbox_create_body(s, &shape, 15.0f + (n % 32) * 9, 20.0f + (n / 32) * 15, 0, false);
        if (!ids[n]) {
            sandbox_destroy(s);
            return 1;
        }
    }
    clock_t start = clock();
    run(s, 300);
    double milliseconds = 1000.0 * (clock() - start) / CLOCKS_PER_SEC;
    printf("%d dynamic objects: %.3f ms/tick (%d remaining)\n", count, milliseconds / 300,
           sandbox_body_count(s));
    bool valid = sandbox_body_count(s) == count;
    for (int n = 0; n < count; ++n) {
        SandboxBodyInfo b = {0};
        if (!sandbox_body_info(s, ids[n], &b) || !isfinite(b.x) || !isfinite(b.y) ||
            !isfinite(b.angle) || !isfinite(b.velocity_x) || !isfinite(b.velocity_y) ||
            b.pixel_count != 36 || b.x < 0 || b.x >= Sandbox_Width || b.y < 0 ||
            b.y >= Sandbox_Height)
            valid = false;
    }
    sandbox_destroy(s);
    return valid ? 0 : 1;
}
int main(int argc, char** argv) {
    if (argc == 3 && strcmp(argv[1], "--benchmark") == 0)
        return benchmark(atoi(argv[2]));
    if (argc == 3 && strcmp(argv[1], "--gallery") == 0)
        return gallery(argv[2]);
    test_shapes_and_placement();
    test_fall_walls_rotation();
    test_object_collisions_and_grab();
    test_particles_and_water();
    test_damage_and_explosions();
    test_limits_and_determinism();
    test_cups_melting_and_demo();
    test_sliding_and_variations();
    test_thrown_shattering();
    if (!failures)
        puts("All rigid body and shape tests passed.");
    return failures ? 1 : 0;
}
