#pragma once

typedef struct Color {
    float r, g, b, a;
} Color;

typedef struct Sprite {
    float x, y, width, height;
    Color color;
} Sprite;
