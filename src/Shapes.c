#include "Sandbox.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

const char* sandbox_shape_name(ShapePreset preset) {
    static const char* const names[] = {
        "Box",  "Disc",      "Triangle", "Oak",    "Boulder", "Pine",       "Birch", "Willow",
        "Palm", "Dead tree", "Crate",    "Barrel", "Bridge",  "Stone arch", "House", "Tower"};
    return (unsigned)preset < ShapePreset_Count ? names[preset] : "Unknown";
}
bool sandbox_shape_varies(ShapePreset preset) {
    return preset >= ShapePreset_Tree && preset <= ShapePreset_DeadTree;
}

void sandbox_load_object_demo(Sandbox* s) {
    sandbox_clear(s);
    for (int y = 150; y < 179; ++y)
        for (int x = 135; x < 219; ++x)
            sandbox_paint(s, x, y, Material_Stone, 0);
    for (int y = 140; y < 150; ++y)
        for (int x = 135; x < 161; ++x)
            sandbox_paint(s, x, y, Material_Stone, 0);
    for (int y = 155; y < 179; ++y)
        for (int x = 220; x < 319; ++x)
            sandbox_paint(s, x, y, Material_Water, 0);
    SandboxShape shape;
    sandbox_shape_preset(&shape, ShapePreset_Tree, Material_Wood);
    sandbox_create_body(s, &shape, 190, 90, 0, false);
    sandbox_shape_preset(&shape, ShapePreset_Boulder, Material_Stone);
    sandbox_create_body(s, &shape, 230, 50, 0, false);
    sandbox_shape_preset(&shape, ShapePreset_Box, Material_Wood);
    sandbox_create_body(s, &shape, 275, 95, 0.15f, false);
    sandbox_paint(s, 250, 40, Material_Sand, 5);
}

void sandbox_shape_paint(SandboxShape* shape, int x, int y, Material material, int radius) {
    if (!shape || x < 0 || y < 0 || x >= Sandbox_ShapeSize || y >= Sandbox_ShapeSize ||
        radius < 0 || radius > 8 ||
        (material != Material_Empty && !sandbox_material_is_rigid(material)))
        return;
    for (int dy = -radius; dy <= radius; ++dy)
        for (int dx = -radius; dx <= radius; ++dx)
            if (x + dx >= 0 && y + dy >= 0 && x + dx < Sandbox_ShapeSize &&
                y + dy < Sandbox_ShapeSize && dx * dx + dy * dy <= radius * radius) {
                int i = (y + dy) * Sandbox_ShapeSize + x + dx;
                shape->pixels[i] = (uint8_t)material;
                shape->colors[i] = 0;
            }
}
static uint32_t shapeRandom(uint32_t* seed) {
    *seed = *seed * UINT32_C(1664525) + UINT32_C(1013904223);
    return *seed;
}
static float shapeUnit(uint32_t* seed) {
    return (shapeRandom(seed) >> 8) / 16777216.0f;
}
static void ellipse(SandboxShape* shape, int cx, int cy, int rx, int ry) {
    for (int y = 0; y < 48; ++y)
        for (int x = 0; x < 48; ++x)
            if ((x - cx) * (x - cx) * ry * ry + (y - cy) * (y - cy) * rx * rx <= rx * rx * ry * ry)
                shape->pixels[y * 48 + x] = Material_Plant;
}
static void branch(SandboxShape* shape, int x0, int y0, int x1, int y1, int radius) {
    int steps = abs(x1 - x0) > abs(y1 - y0) ? abs(x1 - x0) : abs(y1 - y0);
    if (!steps)
        steps = 1;
    for (int n = 0; n <= steps; ++n)
        sandbox_shape_paint(shape, x0 + (x1 - x0) * n / steps, y0 + (y1 - y0) * n / steps,
                            Material_Wood, radius);
}
/* Art and collision share the same pixels, including openings and overhangs. */
static void artPixel(SandboxShape* shape, int x, int y, Material material, uint32_t color) {
    if (x < 0 || x >= 48 || y < 0 || y >= 48)
        return;
    shape->pixels[y * 48 + x] = (uint8_t)material;
    shape->colors[y * 48 + x] = color;
}
static void artRect(SandboxShape* shape, int x0, int y0, int x1, int y1, Material material,
                    uint32_t color) {
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            artPixel(shape, x, y, material, color);
}
static void structureArt(SandboxShape* shape, ShapePreset preset) {
    const uint32_t timber = 0x503C2C, trim = 0x96734A, shadow = 0x302B28;
    for (int y = 0; y < 48; ++y)
        for (int x = 0; x < 48; ++x) {
            Material m = Material_Empty;
            uint32_t c = 0;
            if (preset == ShapePreset_Crate && x >= 9 && x <= 38 && y >= 12 && y <= 40) {
                m = Material_Wood;
                c = x % 6 == 3 ? timber : (x % 6 == 4 ? 0xAD8651 : 0x81603B);
                if (x <= 11 || x >= 36 || y <= 14 || y >= 38 || abs(x - y + 2) <= 1)
                    c = y == 12 || x == 9 ? 0xC39B61 : trim;
                if ((x == 10 || x == 37) && (y == 13 || y == 39)) {
                    m = Material_Steel;
                    c = shadow;
                }
            }
            if (preset == ShapePreset_Barrel && y >= 9 && y <= 41) {
                int width = 10 + (16 * 16 - (y - 25) * (y - 25)) / 75;
                if (abs(x - 24) <= width) {
                    m = Material_Wood;
                    c = x < 19 ? 0xB28B55 : x < 27 ? 0x916937 : 0x61472D;
                    if ((x + 1) % 5 == 0)
                        c = timber;
                    if (y == 9 || y == 10 || y == 40 || y == 41)
                        c = trim;
                    if ((y >= 15 && y <= 17) || (y >= 33 && y <= 35)) {
                        m = Material_Steel;
                        c = y == 15 || y == 33 ? 0x93958B : 0x454B49;
                        if ((x == 16 || x == 28) && (y == 16 || y == 34))
                            c = 0xC1B79A;
                    }
                }
            }
            if (preset == ShapePreset_Bridge && x >= 2 && x <= 45) {
                int deck = 24 + abs(x - 24) / 9;
                if (y >= deck && y <= deck + 3) {
                    m = Material_Wood;
                    c = y == deck ? 0xB39463 : x % 5 == 0 ? shadow : timber;
                }
                if ((x == 5 || x == 6 || x == 40 || x == 41) && y >= 14 && y <= 40) {
                    m = Material_Wood;
                    c = x == 5 || x == 40 ? trim : timber;
                }
                int rope = 15 + (18 * 18 - (x - 23) * (x - 23)) / 90;
                if (x >= 5 && x <= 41 && y >= rope && y <= rope + 1) {
                    m = Material_Wood;
                    c = trim;
                }
                if (y >= 19 && y < deck && (x == 13 || x == 22 || x == 31)) {
                    m = Material_Wood;
                    c = timber;
                }
                if (y > deck && y <= 38 && (abs(x - y + 25) <= 1 || abs(x + y - 71) <= 1)) {
                    m = Material_Wood;
                    c = trim;
                }
            }
            if (preset == ShapePreset_Arch) {
                int dx = x - 24, dy = y - 29;
                bool ring = y <= 29 && dx * dx + dy * dy <= 21 * 21 && dx * dx + dy * dy >= 13 * 13;
                bool pier = y >= 29 && y <= 43 && ((x >= 3 && x <= 11) || (x >= 37 && x <= 45));
                if (ring || pier) {
                    m = Material_Stone;
                    int block = ring ? (int)((atan2f((float)dy, (float)dx) + 3.2f) * 7) : y / 5;
                    c = block % 3 == 0 ? 0x8E8974 : block % 3 == 1 ? 0x777562 : 0xA29A80;
                    if (ring && (int)((atan2f((float)dy, (float)dx) + 3.2f) * 70) % 10 == 0)
                        c = 0x4F5046;
                    if (pier && y % 5 == 0)
                        c = 0x51534A;
                    if (y < 17 && (x + 2 * y) % 7 < 3)
                        c = 0x626D3F;
                }
            }
            if (preset == ShapePreset_House) {
                if (x >= 7 && x <= 40 && y >= 24 && y <= 43) {
                    m = Material_Brick;
                    c = x > 33 ? 0x938169 : 0xB5A587;
                    if (y >= 40) {
                        m = Material_Stone;
                        c = y == 40 ? 0x898471 : 0x625F51;
                    }
                    if (x == 8 || x == 9 || x == 24 || x == 25 || x == 39 || y == 25 || y == 36) {
                        m = Material_Wood;
                        c = timber;
                    }
                    if (y >= 27 && y <= 35 && (abs(x - y + 25) == 0 || abs(x + y - 65) == 0)) {
                        m = Material_Wood;
                        c = trim;
                    }
                }
                if (x >= 33 && x <= 37 && y >= 8 && y <= 24) {
                    m = Material_Brick;
                    c = y % 4 == 0 ? 0x695A4A : 0x94745A;
                }
                int ridge = 19, roof = 7 + abs(x - ridge) * 3 / 4;
                if (x >= 2 && x <= 45 && y >= roof && y <= 24) {
                    m = Material_Wood;
                    c = y == roof                        ? 0xA79266
                        : y == 24                        ? shadow
                        : y % 4 == 0                     ? 0x343F3C
                        : (x + (y / 4 % 2) * 3) % 7 == 0 ? 0x46504A
                                                         : 0x626C58;
                }
            }
            if (preset == ShapePreset_Tower) {
                int width = y >= 40 ? 15 : y >= 35 ? 13 : 11;
                if (y >= 14 && y <= 44 && abs(x - 24) <= width) {
                    m = Material_Stone;
                    c = x < 19 ? 0xAAA18A : x < 29 ? 0x8B8672 : 0x636657;
                    if (y % 5 == 0 || (x + (y / 5 % 2) * 4) % 9 == 0)
                        c = 0x575C51;
                    if (y > 38 && (x * 3 + y) % 9 < 3)
                        c = 0x59623D;
                }
                if (x >= 10 && x <= 38 &&
                    ((y >= 12 && y <= 16) || (y >= 6 && y <= 11 && (x - 10) % 11 < 7))) {
                    m = Material_Stone;
                    c = y == 6 || y == 12 ? 0xB2AA90 : y == 16 ? shadow : 0x828371;
                }
            }
            artPixel(shape, x, y, m, c);
        }
    if (preset == ShapePreset_House) {
        /* Recessed openings have physical sills and timber mullions. */
        for (int x = 13; x <= 31; x += 18) {
            artRect(shape, x - 1, 28, x + 5, 35, Material_Wood, timber);
            artRect(shape, x, 29, x + 4, 33, Material_Empty, 0);
            artRect(shape, x + 2, 29, x + 2, 33, Material_Wood, trim);
            artRect(shape, x - 1, 35, x + 5, 35, Material_Wood, 0xBD9964);
        }
        artRect(shape, 21, 32, 27, 43, Material_Wood, timber);
        artRect(shape, 23, 33, 25, 42, Material_Empty, 0);
        artRect(shape, 20, 43, 28, 44, Material_Stone, 0x8B8975);
        artRect(shape, 32, 7, 38, 8, Material_Brick, 0xB09474);
    } else if (preset == ShapePreset_Tower) {
        for (int y = 21; y <= 31; y += 10) {
            artRect(shape, 21, y, 26, y + 6, Material_Stone, 0xB1A78B);
            artRect(shape, 23, y, 24, y + 5, Material_Empty, 0);
            artRect(shape, 22, y + 2, 25, y + 5, Material_Empty, 0);
        }
        artRect(shape, 21, 39, 27, 44, Material_Empty, 0);
        artRect(shape, 23, 38, 25, 38, Material_Empty, 0);
    } else if (preset == ShapePreset_Arch) {
        artRect(shape, 2, 41, 12, 44, Material_Stone, 0x85836E);
        artRect(shape, 36, 41, 46, 44, Material_Stone, 0x85836E);
        artRect(shape, 21, 7, 27, 13, Material_Stone, 0xB5AC8C);
    }
}
static void shadeNatural(SandboxShape* shape, ShapePreset preset, uint32_t seed) {
    /* Break up ellipse outlines into foliage tufts, preserving the wooden skeleton. */
    if (sandbox_shape_varies(preset) && preset != ShapePreset_Boulder) {
        uint8_t mask[Sandbox_ShapeCells];
        memcpy(mask, shape->pixels, sizeof(mask));
        for (int y = 1; y < 47; ++y)
            for (int x = 1; x < 47; ++x) {
                int i = y * 48 + x;
                uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + seed;
                h = (h ^ (h >> 13)) * 1274126177u;
                if (mask[i] == Material_Plant && (h >> 24) % 4 == 0 &&
                    (!mask[i - 1] || !mask[i + 1] || !mask[i - 48] || !mask[i + 48]))
                    shape->pixels[i] = Material_Empty;
            }
    }
    /* Drop isolated fringe pixels so every procedural asset is one body. */
    uint8_t connected[Sandbox_ShapeCells] = {0};
    int queue[Sandbox_ShapeCells], head = 0, tail = 0;
    int root = 24 * 48 + 24;
    if (!shape->pixels[root])
        for (int i = 0; i < Sandbox_ShapeCells; ++i)
            if (shape->pixels[i]) {
                root = i;
                break;
            }
    if (shape->pixels[root]) {
        queue[tail++] = root;
        connected[root] = 1;
    }
    while (head < tail) {
        int i = queue[head++], x = i % 48, y = i / 48;
        const int neighbors[] = {x ? i - 1 : -1, x < 47 ? i + 1 : -1, y ? i - 48 : -1,
                                 y < 47 ? i + 48 : -1};
        for (int n = 0; n < 4; ++n) {
            int j = neighbors[n];
            if (j >= 0 && !connected[j] && shape->pixels[j]) {
                connected[j] = 1;
                queue[tail++] = j;
            }
        }
    }
    for (int i = 0; i < Sandbox_ShapeCells; ++i)
        if (!connected[i])
            shape->pixels[i] = 0;
    for (int y = 0; y < 48; ++y)
        for (int x = 0; x < 48; ++x) {
            int i = y * 48 + x;
            Material m = (Material)shape->pixels[i];
            if (!m)
                continue;
            if (shape->colors[i])
                continue;
            uint32_t hash = (uint32_t)(x * 73856093u) ^ (uint32_t)(y * 19349663u) ^ seed;
            hash ^= hash >> 13;
            int noise = (int)(hash % 17) - 8, r, g, b;
            if (m == Material_Wood) {
                int grain = ((x + (y / 8)) % 3) * 9;
                r = 103 + grain + noise;
                g = 67 + grain / 2 + noise;
                b = 35 + noise / 2;
                if (preset == ShapePreset_Birch) {
                    bool stripe = (y % 7 == 0 && x % 3 != 0);
                    r = stripe ? 55 : 215 + noise;
                    g = stripe ? 53 : 211 + noise;
                    b = stripe ? 48 : 188 + noise;
                }
            } else if (m == Material_Plant) {
                int light = (48 - x - y) / 4;
                bool edge = y > 0 && !shape->pixels[i - 48];
                uint32_t cluster = (uint32_t)((x + y % 3) / 4) * 374761393u +
                                   (uint32_t)(y / 3) * 668265263u + seed;
                cluster ^= cluster >> 13;
                int patch = (int)(cluster % 4) * 7;
                r = 57 + light + noise / 3 + patch + (edge ? 18 : 0);
                g = 73 + light + noise / 3 + patch + (edge ? 19 : 0);
                b = 30 + light / 2 + noise / 4 + patch / 3;
                if (preset == ShapePreset_Willow) {
                    r += 22;
                    g += 12;
                    b += 8;
                }
                if (preset == ShapePreset_Palm) {
                    r += 8;
                    g += 22;
                }
            } else if (m == Material_Brick) {
                bool mortar = y % 5 == 0 || (x + (y / 5 % 2) * 4) % 9 == 0;
                r = mortar ? 80 : 155 + noise;
                g = mortar ? 75 : 76 + noise;
                b = mortar ? 70 : 53 + noise;
            } else if (m == Material_Steel) {
                r = 95 + noise;
                g = 107 + noise;
                b = 119 + noise;
            } else {
                int face = ((x + y / 2 + (int)(seed % 5)) / 5) % 3;
                int light = (48 - x - y) * 2 + face * 10;
                r = 102 + light + noise;
                g = 105 + light + noise;
                b = 110 + light + noise;
                if ((preset == ShapePreset_Arch || preset == ShapePreset_Tower) &&
                    (y % 6 == 0 || (x + (y / 6 % 2) * 4) % 9 == 0)) {
                    r -= 30;
                    g -= 30;
                    b -= 30;
                }
            }
            if (r < 1)
                r = 1;
            if (g < 1)
                g = 1;
            if (b < 1)
                b = 1;
            if (r > 255)
                r = 255;
            if (g > 255)
                g = 255;
            if (b > 255)
                b = 255;
            shape->colors[i] = (uint32_t)(r << 16 | g << 8 | b);
        }
}
void sandbox_shape_preset(SandboxShape* shape, ShapePreset preset, Material material) {
    sandbox_shape_generate(shape, preset, material, UINT32_C(0x712A39B5));
}
void sandbox_shape_generate(SandboxShape* shape, ShapePreset preset, Material material,
                            uint32_t seed) {
    if (!shape)
        return;
    memset(shape, 0, sizeof(*shape));
    shape->break_speed =
        preset == ShapePreset_Boulder ? 4.0f : (sandbox_shape_varies(preset) ? 3.0f : 0.0f);
    if (!sandbox_material_is_rigid(material))
        material = Material_Wood;
    uint32_t state = seed;
    if (preset == ShapePreset_Birch || preset == ShapePreset_Willow || preset == ShapePreset_Palm ||
        preset == ShapePreset_DeadTree) {
        int lean = (int)(shapeRandom(&state) % 7) - 3;
        if (preset == ShapePreset_Birch) {
            ellipse(shape, 24 + lean, 10, 9, 8);
            ellipse(shape, 17 + lean, 18, 8, 9);
            ellipse(shape, 30 + lean, 19, 8, 9);
            branch(shape, 24, 44, 24 + lean, 12, 1);
            branch(shape, 24 + lean, 28, 15 + lean, 17, 1);
            branch(shape, 24 + lean, 24, 32 + lean, 16, 1);
        } else if (preset == ShapePreset_Willow) {
            ellipse(shape, 24 + lean, 14, 17, 10);
            ellipse(shape, 12 + lean, 19, 8, 7);
            ellipse(shape, 36 + lean, 19, 8, 7);
            for (int x = 8; x <= 40; x += 3) {
                int end = 28 + (int)(shapeRandom(&state) % 9);
                for (int y = 18; y < end; ++y)
                    sandbox_shape_paint(shape, x, y, Material_Plant, 0);
            }
            branch(shape, 25, 44, 23 + lean, 18, 2);
            branch(shape, 24 + lean, 24, 13 + lean, 17, 1);
            branch(shape, 24 + lean, 24, 35 + lean, 17, 1);
        } else if (preset == ShapePreset_Palm) {
            branch(shape, 22, 44, 25 + lean, 15, 1);
            for (int side = -1; side <= 1; side += 2)
                for (int frond = 0; frond < 3; ++frond) {
                    int end = side * (14 + frond * 2), drop = frond * 7 - 5;
                    for (int n = 0; n <= 20; ++n) {
                        int x = 25 + lean + end * n / 20;
                        int y = 15 + drop * n / 20 - (20 - n) * n / 60;
                        sandbox_shape_paint(shape, x, y, Material_Plant, 1);
                    }
                }
            for (int y = 8; y < 16; ++y)
                sandbox_shape_paint(shape, 25 + lean, y, Material_Plant, 1);
        } else {
            branch(shape, 24, 44, 22 + lean, 12, 2);
            branch(shape, 24, 33, 11 + lean, 22, 1);
            branch(shape, 11 + lean, 22, 9 + lean, 13, 1);
            branch(shape, 23, 25, 36 + lean, 17, 1);
            branch(shape, 36 + lean, 17, 39 + lean, 9, 1);
            branch(shape, 22 + lean, 17, 15 + lean, 8, 1);
            branch(shape, 22 + lean, 14, 26 + lean, 4, 1);
            branch(shape, 31 + lean, 20, 32 + lean, 10, 1);
        }
        shadeNatural(shape, preset, seed);
        return;
    }
    if (preset >= ShapePreset_Crate && preset < ShapePreset_Count) {
        structureArt(shape, preset);
        shadeNatural(shape, preset, seed);
        return;
    }
    if (preset == ShapePreset_Tree) {
        const int lobes[][4] = {{23, 17, 13, 11}, {12, 17, 8, 7}, {19, 10, 9, 7}, {30, 10, 8, 7},
                                {36, 18, 8, 8},   {29, 24, 9, 6}, {13, 24, 8, 6}};
        for (int n = 0; n < 7; ++n)
            ellipse(shape, lobes[n][0] + (int)(shapeRandom(&state) % 3) - 1,
                    lobes[n][1] + (int)(shapeRandom(&state) % 3) - 1, lobes[n][2], lobes[n][3]);
        int lean = (int)(shapeRandom(&state) % 5) - 2;
        branch(shape, 24, 43, 24 + lean, 25, 2);
        branch(shape, 24 + lean, 30, 17 + lean, 17, 1);
        branch(shape, 24 + lean, 28, 32 + lean, 17, 1);
        branch(shape, 22 + lean, 25, 12, 21, 1);
        branch(shape, 27 + lean, 24, 35, 22, 1);
        branch(shape, 24, 40, 19, 44, 1);
        branch(shape, 25, 40, 29, 44, 1);
        for (int n = 0; n < 24; ++n) {
            int x = 7 + (int)(shapeRandom(&state) % 35);
            int y = 6 + (int)(shapeRandom(&state) % 22);
            if (shape->pixels[y * 48 + x] == Material_Plant)
                ellipse(shape, x, y, 2 + (int)(shapeRandom(&state) % 3), 2);
        }
        shadeNatural(shape, preset, seed);
        return;
    }
    if (preset == ShapePreset_Pine) {
        int lean = (int)(shapeRandom(&state) % 7) - 3;
        /* A bent leader with independently grown, upswept tips on drooping boughs. */
        branch(shape, 24, 45, 23, 29, 1);
        branch(shape, 23, 29, 24 + lean, 3, 1);
        for (int layer = 0; layer < 5; ++layer) {
            int joint = 9 + layer * 6;
            int trunk = 24 + lean * (45 - joint) / 42;
            for (int side = -1; side <= 1; side += 2) {
                int length = 4 + layer * 3 + (int)(shapeRandom(&state) % 4);
                int drop = 3 + (int)(shapeRandom(&state) % 3);
                int offset = (int)(shapeRandom(&state) % 3) - 1;
                for (int n = 0; n <= length; ++n) {
                    float t = (float)n / length;
                    int x = trunk + side * n;
                    int bottom = joint + offset + (int)(drop * sinf(t * 2.3f));
                    int depth = 1 + (int)((1 - t) * (5 + layer / 2));
                    if (n % 3 == 1)
                        ++depth;
                    for (int y = bottom - depth; y <= bottom; ++y)
                        artPixel(shape, x, y, Material_Plant, 0);
                    /* Small twig glimpses; the silhouette stays leafy. */
                    if (n < length - 3 && n % 4 < 2)
                        artPixel(shape, x, bottom - 1, Material_Wood, 0);
                }
            }
        }
        for (int y = 1; y <= 12; ++y) {
            int x = 24 + lean * (45 - y) / 42;
            int width = (y - 1) / 4;
            for (int dx = -width; dx <= width; ++dx)
                artPixel(shape, x + dx, y, Material_Plant, 0);
        }
        branch(shape, 24, 44, 21, 46, 1);
        branch(shape, 24, 44, 27, 46, 1);
        shadeNatural(shape, preset, seed);
        return;
    }
    float radius = 10 + shapeUnit(&state) * 4, aspect = 0.72f + shapeUnit(&state) * 0.4f;
    float phase = shapeUnit(&state) * 6.2831853f, phase2 = shapeUnit(&state) * 6.2831853f;
    for (int y = 0; y < Sandbox_ShapeSize; ++y)
        for (int x = 0; x < Sandbox_ShapeSize; ++x) {
            int dx = x - 24, dy = y - 24;
            Material m = Material_Empty;
            if (preset == ShapePreset_Box && x >= 12 && x <= 35 && y >= 18 && y <= 29)
                m = material;
            if (preset == ShapePreset_Disc && dx * dx + dy * dy <= 100)
                m = material;
            if (preset == ShapePreset_Triangle && y >= 12 && y <= 35 && dx * 2 >= -(y - 12) &&
                dx * 2 <= y - 12)
                m = material;
            if (preset == ShapePreset_Boulder) {
                float fy = dy / aspect, angle = atan2f(fy, (float)dx);
                float edge =
                    radius * (1 + 0.13f * sinf(3 * angle + phase) +
                              0.08f * cosf(2 * angle + phase2) + 0.04f * sinf(5 * angle + phase));
                if (dx * dx + fy * fy <= edge * edge)
                    m = Material_Stone;
            }
            shape->pixels[y * Sandbox_ShapeSize + x] = (uint8_t)m;
        }
    if (preset == ShapePreset_Boulder)
        shadeNatural(shape, preset, seed);
}
