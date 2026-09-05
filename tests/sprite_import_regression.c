#include "SpriteImport.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int failures;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c);                                   \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)
static void writeXml(const char* text) {
    FILE* f = NULL;
    fopen_s(&f, "sprite-test.xml", "wb");
    CHECK(f != NULL);
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}
static void rejected(const char* xml) {
    SandboxShape shape, before;
    memset(&shape, 0x5A, sizeof(shape));
    before = shape;
    bool fixed = true;
    char error[128] = {0};
    writeXml(xml);
    CHECK(!sprite_import_xml("sprite-test.xml", &shape, &fixed, error, sizeof(error)));
    CHECK(!memcmp(&shape, &before, sizeof(shape)) && fixed && error[0]);
}
int main(int argc, char** argv) {
    CHECK(argc == 3);
    if (argc != 3)
        return 1;
    char path[4096], xml[8192], error[128];
    SandboxShape shape = {0};
    bool fixed = true;
    snprintf(path, sizeof(path), "%s/example.xml", argv[1]);
    CHECK(sprite_import_xml(path, &shape, &fixed, error, sizeof(error)));
    CHECK(!fixed && shape.break_speed == 3);
    int plants = 0, wood = 0, black = 0;
    for (int i = 0; i < Sandbox_ShapeCells; ++i) {
        plants += shape.pixels[i] == Material_Plant;
        wood += shape.pixels[i] == Material_Wood;
        black += shape.colors[i] == 0x01000000;
    }
    CHECK(plants > 0 && wood > 0 && black > 0);
    Sandbox* sandbox = sandbox_create();
    CHECK(sandbox_can_place_shape(sandbox, &shape, 100, 60, 0));
    CHECK(sandbox_create_body(sandbox, &shape, 100, 60, 0, false) != 0);
    size_t sprite_count = 0;
    const Sprite* sprites = sandbox_build_sprites(sandbox, 320, 180, &sprite_count);
    int rendered_black = 0;
    for (size_t n = 0; n < sprite_count; ++n)
        if (sprites[n].color.r == 0 && sprites[n].color.g == 0 && sprites[n].color.b == 0)
            ++rendered_black;
    CHECK(rendered_black > 0);
    sandbox_destroy(sandbox);
    snprintf(xml, sizeof(xml),
             "<sprite image='%s/palette.png' material='Stone' fixed='true' break_speed='4'/>",
             argv[2]);
    writeXml(xml);
    CHECK(sprite_import_xml("sprite-test.xml", &shape, &fixed, error, sizeof(error)));
    CHECK(fixed && shape.break_speed == 4 && shape.pixels[23 * 48 + 23] == Material_Stone);
    CHECK(shape.colors[23 * 48 + 23] == 0x01000000);
    CHECK(shape.pixels[23 * 48 + 24] == 0); /* Indexed transparency. */
    snprintf(xml, sizeof(xml), "<sprite image='%s/a&amp;b.png' alpha_threshold='200'/>", argv[2]);
    writeXml(xml);
    CHECK(sprite_import_xml("sprite-test.xml", &shape, &fixed, error, sizeof(error)));
    CHECK(!fixed && shape.break_speed == 0);
    CHECK(shape.pixels[23 * 48 + 23] == Material_Wood && shape.pixels[23 * 48 + 24] == 0);
    snprintf(xml, sizeof(xml), "<sprite image='%s/caf\xC3\xA9.png'/>\n", argv[2]);
    writeXml(xml);
    CHECK(sprite_import_xml("sprite-test.xml", &shape, &fixed, error, sizeof(error)));
    const char* invalid[] = {
        "",
        "<sprite>",
        "<sprite/>",
        "<wrong/>",
        "<sprite image='missing.png'/>",
        "<sprite image='x' fixed='yes'/>",
        "<sprite image='x' material='Water'/>",
        "<sprite image='x' material='bogus'/>",
        "<sprite image='x' break_speed='nan'/>",
        "<sprite image='x' break_speed='-1'/>",
        "<sprite image='x' alpha_threshold='0'/>",
        "<sprite image='x' alpha_threshold='256'/>",
        "<sprite image='x' unknown='1'/>",
        "<sprite image='x'><map color='#nothex' material='Wood'/></sprite>",
        "<sprite image='x'><map color='#000000'/></sprite>",
        "<sprite image='x'><map color='#000000' material='Wood'/><map color='#000000' "
        "material='Stone'/></sprite>",
        "<sprite image='x'><map color='#000000' material='Wood'><bad/></map></sprite>",
        "<!DOCTYPE sprite [<!ENTITY x SYSTEM 'file:///missing'>]><sprite image='&x;'/>",
        "<sprite image='x'/>trailing"};
    for (size_t n = 0; n < sizeof(invalid) / sizeof(invalid[0]); ++n)
        rejected(invalid[n]);
    const char* bad_images[] = {"oversize.png", "empty.png", "disconnected.png", "broken.png"};
    for (size_t n = 0; n < sizeof(bad_images) / sizeof(bad_images[0]); ++n) {
        snprintf(xml, sizeof(xml), "<sprite image='%s/%s'/>", argv[2], bad_images[n]);
        rejected(xml);
    }
    remove("sprite-test.xml");
    printf("Sprite import: %d failures\n", failures);
    return failures ? 1 : 0;
}
