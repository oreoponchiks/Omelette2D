# Import your own sprites

Open **Shape Creator / Object workshop**, choose **Browse XML...**, and select your XML file. The PNG is loaded automatically. Inspect or edit the preview, then choose **Place on canvas**. Imports disable preset variation so repeated placements preserve your artwork. Failed imports leave your previous template intact.

Try `assets/sprites/example.xml` first. The PNG and XML are also visible under Resource Files in the Visual Studio project.

```xml
<?xml version="1.0" encoding="utf-8"?>
<sprite image="my_tree.png" material="Wood" fixed="false" break_speed="3" alpha_threshold="128">
  <map color="#526B32" material="Plant" />
  <map color="#79502D" material="Wood" />
</sprite>
```

- `image` is required. Relative paths resolve beside the XML, regardless of the engine's working directory. Absolute paths and Unicode filenames also work. XML escapes such as `&amp;` work in filenames.
- PNG size: **1?48 pixels on each axis**. Smaller images are centered without scaling. Export pixel art without antialiasing. RGB, RGBA, and palette PNGs are decoded to RGBA.
- `material` defaults to `Wood`. Each optional `map` assigns an exact `#RRGGBB` color to a material. Unmapped visible colors use the default material. Colors remain the PNG's original colors, including black.
- Material names match the in-game material picker (case-insensitive). Only rigid materials are accepted: for example Wood, Plant, Stone, Brick, Steel, Glass, Ice. Liquids, gases, and loose powders cannot form rigid sprites.
- `fixed` defaults to `false`. Use `true` for anchored buildings; the workshop checkbox can override it.
- `break_speed` defaults to `0` (no throw shattering). Try `3` for trees or `4` for rocks. This uses the existing throw-impact system; heat, explosions, and cutting retain their usual behavior.
- `alpha_threshold` defaults to `128`, range 1?255. Pixels below it are empty; accepted pixels become fully opaque. There is no blended collision layer.
- Solid pixels must form one edge-connected object. Transparent holes are allowed; disconnected islands or an entirely transparent image are rejected. Split separate objects into separate PNG/XML pairs.

The format is specific to Omelette2D; existing Noita XML definitions are not directly compatible. Unknown attributes, duplicate color mappings, malformed XML, and unsupported content are rejected with an error in the workshop. This importer does not add animation or sprite sheets.

Implementation uses the Windows [WIC decoder](https://learn.microsoft.com/en-us/windows/win32/wic/-wic-decoder-howto-createusingfilename) and XmlLite through their C interfaces; no new C++ engine code or downloaded dependency is needed.
