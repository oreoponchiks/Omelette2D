#define COBJMACROS
#include "SpriteImport.h"
#include <math.h>
#include <shlwapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wincodec.h>
#include <windows.h>
#include <xmllite.h>

static bool materialName(const wchar_t* name, Material* result) {
    for (int n = 1; n < Material_Count; ++n) {
        wchar_t wide[80];
        MultiByteToWideChar(CP_UTF8, 0, sandbox_material_name((Material)n), -1, wide, 80);
        if (!_wcsicmp(name, wide) && sandbox_material_is_rigid((Material)n)) {
            *result = (Material)n;
            return true;
        }
    }
    return false;
}
static bool connected(const SandboxShape* shape) {
    int queue[Sandbox_ShapeCells], total = 0, head = 0, tail = 0;
    bool seen[Sandbox_ShapeCells] = {0};
    for (int i = 0; i < Sandbox_ShapeCells; ++i)
        if (shape->pixels[i]) {
            if (!total) {
                queue[tail++] = i;
                seen[i] = true;
            }
            ++total;
        }
    while (head < tail) {
        int i = queue[head++], x = i % 48, y = i / 48;
        int adjacent[] = {x ? i - 1 : -1, x < 47 ? i + 1 : -1, y ? i - 48 : -1,
                          y < 47 ? i + 48 : -1};
        for (int n = 0; n < 4; ++n) {
            int j = adjacent[n];
            if (j >= 0 && shape->pixels[j] && !seen[j]) {
                seen[j] = true;
                queue[tail++] = j;
            }
        }
    }
    return total && tail == total;
}
bool sprite_import_xml(const char* path, SandboxShape* output, bool* fixed, char* error,
                       size_t error_size) {
    const char* message = "Cannot read sprite XML.";
    bool ok = false, root = false, fixed_value = false;
    SandboxShape shape = {0};
    Material fallback = Material_Wood, materials[256];
    uint32_t colors[256];
    int mappings = 0, alpha = 128;
    wchar_t xml_path[32768], image_path[32768] = {0}, image_name[32768] = {0};
    IStream* stream = NULL;
    IXmlReader* reader = NULL;
    IWICImagingFactory* factory = NULL;
    IWICBitmapDecoder* decoder = NULL;
    IWICBitmapFrameDecode* frame = NULL;
    IWICFormatConverter* converter = NULL;
    HRESULT com = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED), hr;
    if (!path || !output || !fixed ||
        !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, xml_path, 32768))
        goto done;
    if (FAILED(com) && com != RPC_E_CHANGED_MODE)
        goto done;
    if (FAILED(SHCreateStreamOnFileEx(xml_path, STGM_READ | STGM_SHARE_DENY_WRITE, 0, FALSE, NULL,
                                      &stream)))
        goto done;
    STATSTG stat;
    message = "Sprite XML must be at most 256 KiB.";
    if (FAILED(IStream_Stat(stream, &stat, STATFLAG_NONAME)) || stat.cbSize.QuadPart > 256 * 1024)
        goto done;
    message = "Cannot read sprite XML.";
    if (FAILED(CreateXmlReader(&IID_IXmlReader, (void**)&reader, NULL)))
        goto done;
    IXmlReader_SetProperty(reader, XmlReaderProperty_DtdProcessing, DtdProcessing_Prohibit);
    IXmlReader_SetProperty(reader, XmlReaderProperty_MaxElementDepth, 4);
    if (FAILED(IXmlReader_SetInput(reader, (IUnknown*)stream)))
        goto done;
    XmlNodeType type;
    while ((hr = IXmlReader_Read(reader, &type)) == S_OK) {
        if (type == XmlNodeType_Element) {
            const wchar_t* name;
            UINT depth;
            IXmlReader_GetDepth(reader, &depth);
            IXmlReader_GetLocalName(reader, &name, NULL);
            bool is_root = depth == 0 && !root && !wcscmp(name, L"sprite");
            bool is_map = depth == 1 && root && !wcscmp(name, L"map");
            message = "Expected <sprite> with empty <map color=... material=.../> entries.";
            if (!is_root && !is_map)
                goto done;
            if (is_map && (!IXmlReader_IsEmptyElement(reader) || mappings == 256))
                goto done;
            bool have_color = false, have_material = false;
            uint32_t color = 0;
            Material material = Material_Empty;
            HRESULT attr = IXmlReader_MoveToFirstAttribute(reader);
            while (attr == S_OK) {
                const wchar_t *key, *value;
                IXmlReader_GetQualifiedName(reader, &key, NULL);
                IXmlReader_GetValue(reader, &value, NULL);
                message = "Unknown or invalid XML attribute.";
                if (is_root && !wcscmp(key, L"image")) {
                    if (!*value || wcslen(value) >= 32768)
                        goto done;
                    wcscpy_s(image_name, 32768, value);
                } else if (is_root && !wcscmp(key, L"material")) {
                    message = "Unknown material or non-rigid material.";
                    if (!materialName(value, &fallback))
                        goto done;
                } else if (is_root && !wcscmp(key, L"fixed")) {
                    if (!wcscmp(value, L"true"))
                        fixed_value = true;
                    else if (wcscmp(value, L"false"))
                        goto done;
                } else if (is_root && !wcscmp(key, L"break_speed")) {
                    wchar_t* end;
                    double speed = wcstod(value, &end);
                    if (end == value || *end || !isfinite(speed) || speed < 0 || speed > 10000)
                        goto done;
                    shape.break_speed = (float)speed;
                } else if (is_root && !wcscmp(key, L"alpha_threshold")) {
                    wchar_t* end;
                    long n = wcstol(value, &end, 10);
                    if (end == value || *end || n < 1 || n > 255)
                        goto done;
                    alpha = (int)n;
                } else if (is_map && !wcscmp(key, L"color")) {
                    if (wcslen(value) != 7 || value[0] != L'#')
                        goto done;
                    for (int n = 1; n < 7; ++n) {
                        wchar_t c = value[n];
                        int digit = c >= L'0' && c <= L'9'   ? c - L'0'
                                    : c >= L'a' && c <= L'f' ? c - L'a' + 10
                                    : c >= L'A' && c <= L'F' ? c - L'A' + 10
                                                             : -1;
                        if (digit < 0)
                            goto done;
                        color = (color << 4) | (uint32_t)digit;
                    }
                    have_color = true;
                } else if (is_map && !wcscmp(key, L"material")) {
                    message = "Unknown material or non-rigid material.";
                    if (!materialName(value, &material))
                        goto done;
                    have_material = true;
                } else
                    goto done;
                attr = IXmlReader_MoveToNextAttribute(reader);
            }
            if (FAILED(attr))
                goto done;
            IXmlReader_MoveToElement(reader);
            if (is_root)
                root = true;
            else {
                message = "Each map needs a unique #RRGGBB color and rigid material.";
                if (!have_color || !have_material)
                    goto done;
                for (int n = 0; n < mappings; ++n)
                    if (colors[n] == color)
                        goto done;
                colors[mappings] = color;
                materials[mappings++] = material;
            }
        } else if (type != XmlNodeType_EndElement && type != XmlNodeType_Whitespace &&
                   type != XmlNodeType_Comment && type != XmlNodeType_XmlDeclaration) {
            message = "Unexpected XML content.";
            goto done;
        }
    }
    message = "Malformed XML or missing sprite image attribute.";
    if (FAILED(hr) || !root || !image_name[0])
        goto done;
    if (PathIsRelativeW(image_name)) {
        wchar_t* slash = wcsrchr(xml_path, L'\\');
        wchar_t* forward = wcsrchr(xml_path, L'/');
        if (forward && (!slash || forward > slash))
            slash = forward;
        if (slash) {
            size_t prefix = (size_t)(slash - xml_path) + 1;
            wcsncpy_s(image_path, 32768, xml_path, prefix);
        }
        message = "Combined PNG path is too long.";
        if (wcslen(image_path) + wcslen(image_name) >= 32768)
            goto done;
        wcscat_s(image_path, 32768, image_name);
    } else
        wcscpy_s(image_path, 32768, image_name);
    message = "Cannot decode sprite PNG. Check the image path relative to the XML.";
    if (FAILED(CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                                &IID_IWICImagingFactory, (void**)&factory)))
        goto done;
    if (FAILED(IWICImagingFactory_CreateDecoderFromFilename(
            factory, image_path, NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)))
        goto done;
    GUID format;
    if (FAILED(IWICBitmapDecoder_GetContainerFormat(decoder, &format)) ||
        !IsEqualGUID(&format, &GUID_ContainerFormatPng))
        goto done;
    if (FAILED(IWICBitmapDecoder_GetFrame(decoder, 0, &frame)))
        goto done;
    UINT width, height;
    if (FAILED(IWICBitmapFrameDecode_GetSize(frame, &width, &height)))
        goto done;
    message = "PNG must be between 1x1 and 48x48 pixels. Resize in your art editor.";
    if (!width || !height || width > 48 || height > 48)
        goto done;
    message = "Could not convert PNG pixels.";
    if (FAILED(IWICImagingFactory_CreateFormatConverter(factory, &converter)))
        goto done;
    if (FAILED(IWICFormatConverter_Initialize(
            converter, (IWICBitmapSource*)frame, &GUID_WICPixelFormat32bppRGBA,
            WICBitmapDitherTypeNone, NULL, 0, WICBitmapPaletteTypeCustom)))
        goto done;
    BYTE rgba[48 * 48 * 4];
    message = "Could not read PNG pixels.";
    if (FAILED(IWICFormatConverter_CopyPixels(converter, NULL, width * 4, sizeof(rgba), rgba)))
        goto done;
    for (UINT y = 0; y < height; ++y)
        for (UINT x = 0; x < width; ++x) {
            BYTE* pixel = &rgba[(y * width + x) * 4];
            if (pixel[3] < alpha)
                continue;
            uint32_t color = ((uint32_t)pixel[0] << 16) | ((uint32_t)pixel[1] << 8) | pixel[2];
            Material material = fallback;
            for (int n = 0; n < mappings; ++n)
                if (colors[n] == color) {
                    material = materials[n];
                    break;
                }
            int i = ((48 - (int)height) / 2 + (int)y) * 48 + (48 - (int)width) / 2 + (int)x;
            shape.pixels[i] = (uint8_t)material;
            shape.colors[i] = color ? color : UINT32_C(0x01000000); /* Explicit black tint. */
        }
    message = "Sprite must contain one connected solid shape (edge-connected pixels).";
    if (!connected(&shape))
        goto done;
    *output = shape;
    *fixed = fixed_value;
    ok = true;
    message = "Sprite imported. Choose Place on canvas.";
done:
    if (converter)
        IWICFormatConverter_Release(converter);
    if (frame)
        IWICBitmapFrameDecode_Release(frame);
    if (decoder)
        IWICBitmapDecoder_Release(decoder);
    if (factory)
        IWICImagingFactory_Release(factory);
    if (reader)
        IXmlReader_Release(reader);
    if (stream)
        IStream_Release(stream);
    if (SUCCEEDED(com))
        CoUninitialize();
    if (error && error_size)
        snprintf(error, error_size, "%s", message);
    return ok;
}
