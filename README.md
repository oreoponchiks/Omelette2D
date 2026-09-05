# Omelette2D

A compact C17/Vulkan falling-sand engine with destructible rigid objects for Windows. It uses native Win32, Vulkan 1.3 dynamic rendering, Dear ImGui, persistently mapped per-frame instance data, alpha blending, and batched cellular-material rendering.

All engine code in `src/` is C: the window loop, simulation, Vulkan renderer, and UI setup. The engine calls [cimgui](https://github.com/cimgui/cimgui) directly through its generated C API. The pinned binding matches the existing Dear ImGui 1.92.8 library, preserving the UI appearance and behavior. cimgui and Dear ImGui remain third-party C++ dependencies, so building the bundled UI still requires a C++ compiler. See `third_party/cimgui/README.md` for the revision and integration details.

Runtime defaults live in [`config.toml`](config.toml). It controls the window, initial scene and view, selected tool and material, brush strengths, and the first-use UI panel size. Edit the source file and rebuild; the build copies it beside `Omelette2D.exe`. You can also edit that copied file for a quick local change, though the next build may replace it. Restart the app to apply changes. Missing files use built-in defaults, while invalid or unknown settings produce a startup error so typos are visible.

In Visual Studio, engine files (including `Ui.c`) are under **Source Files**, and the bindings are under **Dependencies > cimgui**. The old custom `imgui_c.cpp` wrapper has been removed.

## Build

Install the Vulkan SDK and Visual Studio's **Desktop development with C++** workload, then:

```powershell
cmake --preset windows
cmake --build --preset debug
.\build\Debug\Omelette2D.exe
```

The preset selects the installed Visual Studio 2019 v142 toolchain with C17 support. Visual Studio can also open the folder as a CMake project, or open `Omelette2D.slnx` in a version supporting that solution format. The native project explicitly compiles the engine `.c` files as C17 and writes its executable to `build/x64/Debug/` or `build/x64/Release/`.

The sandbox uses a 320x180 grid with **64 materials plus the eraser**: the original 14 materials and **50 additions**, including five bomb types, TNT, gunpowder, and fuse. The scrollable picker has category filters, case-insensitive name search, and behavior hints. See [the additional material catalog](MATERIALS.md) for all 50 additions. Their names, properties, colors, and hints live in `src/Materials.def`, shown under **Header Files** in Visual Studio.

Left mouse applies the selected material/tool; right mouse erases. Ctrl + mouse wheel adjusts the brush. Pause, Clear, and Step let you inspect reactions one tick at a time.

## Simulation

The simulation now uses Powder Toy-inspired material properties and interactions, implemented in C:

- **Momentum and collisions:** powders, liquids, and gases carry velocity and accelerate. Swept movement checks every crossed cell, including thin obstacles. Sand settles into piles and sinks through lighter liquids.
- **Liquids and gases:** densities separate oil and water; viscosity controls lateral spreading. Smoke and steam rise, diffuse, and respond to air motion. Sand and water remain separate instead of immediately becoming mud.
- **Heat:** cells exchange heat according to conductivity and heat capacity, with cooling toward a 20 C environment. Water freezes below -2 C and boils at 105 C; ice melts at 2 C; steam condenses below 95 C. These thresholds include hysteresis to reduce flickering between phases. Lava solidifies below 700 C, stone melts above 1050 C, and very hot sand melts into lava.
- **Combustion and reactions:** hot wood/plants burn in place while consuming fuel and emitting flames. Water quenches fire. Hot oil ignites and releases pressure. Acid has finite strength; diluted acid cannot reproduce through water. Plant growth consumes water, and hot mud dries into sand.
- **Air:** a bounded, damped pressure/velocity field runs on 4x4-cell tiles. Fire and boiling inject pressure and hot gas produces upward flow. Solid-containing tiles block air conservatively, so air obstacles have coarser resolution than particle collisions. Pressure uses relative simulation units.
- **New materials:** salt and sugar dissolve and leave solids when their solutions boil. Wax, metals, and glass melt and solidify. Clay fires into brick, soil becomes mud, wet seeds germinate, and moss/algae consume water to spread. New fuels burn into ash or smoke; oxygen supports fire and carbon dioxide quenches it. Glass and concrete fracture under pressure. Different densities, conductivity, buoyancy, and viscosity give powders, liquids, gases, and construction materials different uses.

## Bombs

Select **Explosives** in the category picker, or search for **Bomb**:

- **Timed Bomb:** falls and detonates after 3 simulation seconds.
- **Impact Bomb:** detonates on a hard landing; can also be triggered by heat or another blast.
- **Remote Bomb:** stays where placed. **Detonate remote bombs** arms all existing remote charges for the next simulation step.
- **Fire Bomb:** a 3-second timer releases a hot flame cloud.
- **Cryo Bomb:** a 3-second timer releases cold vapor and freezes surrounding matter.
- **TNT / Gunpowder / Fuse:** heat ignites powder or TNT; burning fuse carries ignition to adjoining charges. Hydrogen and natural gas also produce small blasts when heated.

Blasts inject outward particle velocity and air pressure, break nearby solids into debris, and trigger chain reactions. Steel and heavy masonry resist weaker portions of a blast. Cryogenic blasts cool instead of igniting neighbors. The radial blast is a local game effect, not a physically accurate model of shielding; ordinary air pressure still obeys the air obstacle grid. At most 64 charges explode per tick; excess armed charges continue on following ticks, avoiding recursive chain reactions. Clear removes pending charges. Timers and armed charges wait while paused; use **Step** or uncheck **Paused** to continue.

Use **Heat**, **Cool**, and **Air** tools to test reactions, or select a material to return to painting. Tool strength scales with elapsed time. **Heat view** colors particles by temperature; **Pressure view** shows positive pressure in red and negative pressure in blue. The cursor readout reports material, Celsius temperature, and local pressure. Painting over the same material preserves its current heat and momentum.

The model is inspired by [The Powder Toy's material properties, heat, and air simulation](https://github.com/The-Powder-Toy/The-Powder-Toy). Its constants are tuned for this engine's grid. The 60 Hz fixed step remains independent of rendering frequency; each update accepts up to 0.1 seconds of catch-up time.

## Objects and shape creator

**Oak**, **Pine**, and **Boulder** select ready-made movable objects. Click clear space in the world to place one; each click creates one object. **Object** places the current workshop template. **Grab** lets you drag a loose object with a spring, including pulling it off-center to rotate it. Right mouse still erases, cutting through terrain and object pixels. **Clear + object demo** replaces the world with a small terrain, tree, boulder, sand, water, and floating-box playground.

Open **Shape creator** for the **Object workshop**:

1. Load a box, disc, triangle, oak, pine, boulder, birch, willow, palm, dead tree, crate, barrel, bridge, stone arch, house, or tower preset, or clear the 48x48 canvas.
2. Choose a solid material and draw with the left mouse; erase with the right mouse. Different parts can use different materials. Keep the pixels connected; concave shapes and hollow cups are supported.
3. Set placement rotation and optionally **Fixed in place**. Click **Place on canvas**, then click clear space in the world. Green preview means the shape fits; red means overlap, invalid/disconnected geometry, or no free object slot.

Natural presets have **Vary each placement** enabled: every successful placement prepares a new seeded silhouette for the next preview. **New variation** rerolls the current preset. Turning variation off repeats the same shape; drawing in the editor automatically turns it off to preserve custom work. Oaks have spreading crowns, branching trunks, and roots; pines have bent leaders, asymmetric drooping boughs, and upturned tips inspired by the reference forest. Tree crowns use ragged foliage edges and clustered olive shading. Boulders vary in outline, proportions, and shaded facets. Shading is stored on object pixels, so bark, foliage, and rock texture rotate with the object.

**Throw to shatter:** use **Grab**, move quickly, and release an oak, pine, or boulder. A sufficiently fast closing impact against terrain, screen boundaries, or another object fractures it into several rigid chunks plus gravel, wood splinters, and leafy dust. Debris keeps its color and inherits motion; chunks separate and keep colliding. Gentle drops and resting contacts stay intact. Placement alone does not arm throw damage. Natural presets use closing-speed thresholds of 3 cells/tick for trees and 4 for boulders; custom templates can opt in through `SandboxShape.break_speed`. Fracturing is deferred until collision solving finishes, and a full object pool turns excess fragments into loose debris.

**More trees and structures:** birches have pale bark, willows have hanging foliage, palms have spreading fronds, and dead trees have bare branches. The workshop also includes a planked and braced crate, riveted barrel, timber-supported rope bridge, weathered stone arch, timber-framed cottage with a shingled roof and chimney, and a buttressed masonry tower. Roofs, sills, beams, bark, and masonry shading are part of the rotating destructible pixels. **Structures** opens the workshop on a house with **Fixed in place** enabled; turn that off to make structures movable. Hollow interiors, windows, and arch openings use the actual collision mask.

The template remains available for repeated placement during the session. Pause freezes rigid physics along with particles; Step advances both. Use heat, acid, fire, or bombs on placed objects: burning/melting pixels return to the particle simulation, cuts split masks into independent fragments, and explosions push and rotate surviving pieces.

Rigid bodies have material-dependent mass, a center of mass, angular inertia, gravity, friction, and contact impulses. Sub-cell movement sweeps prevent fast objects from jumping through thin walls. When a combined movement is blocked, translation axes and rotation are solved separately so a blocked corner cannot cancel otherwise free sliding. A collision-checked separation of at most half a cell handles interlocking raster edges; screen side contacts do not apply support friction. Objects collide with terrain and other objects, stack, and present their actual pixel masks to particles. Moving bodies relocate displaced powders/liquids to available cells instead of deleting them; if no destination is available they stop. Buoyancy estimates submerged volume from neighboring liquid rows, so wood floats while stone sinks. It is a grid-based approximation, with up to 256 bodies and 48x48 source pixels per body. Rotation is rasterized at the world grid's resolution. If damage creates more fragments than available slots, excess fragments become loose debris.

The design is inspired by the integration of falling-sand materials and destructible rigid bodies described in [Petri Purho's Noita GDC talk](https://www.gdcvault.com/play/1025695/Exploring-the-Tech-and-DesignAt). It uses this project's own C solver. `src/RigidBodies.inc` contains the solver included by `Sandbox.c`; `Shapes.c` supplies masks/presets, and `ShapeEditor.c` implements the cimgui workshop. These files are included in both CMake and the native Visual Studio project.

The object limit is **256** (previously 32). Grid ownership uses 16-bit indices so slot 256 is represented correctly. Buoyancy rows are collected in one shared grid pass, and connectivity/mass are rebuilt only when material geometry changes. Heat, burning, and damage still update every tick. On this workstation, the same Release benchmark with 32 dynamic 6x6 blocks dropped from about **1.25 to 0.72 ms per simulation tick**. A 256-block run averaged **13.35 ms/tick** over 300 ticks, with all bodies retained. These are physics-only measurements for those scenes, not a frame-rate guarantee for arbitrary large masks or fracture storms.

```powershell
.\build\Release\RigidBodyTests.exe --benchmark 32
.\build\Release\RigidBodyTests.exe --benchmark 256
```

To start directly in the playground with the workshop open:

```powershell
.\build\x64\Release\Omelette2D.exe --object-demo
```

## Tests

```powershell
ctest --test-dir build -C Debug --output-on-failure
cmake --build --preset release
ctest --test-dir build -C Release --output-on-failure
```

`SandboxTests` checks acceleration, thin-wall collisions, particle conservation, density sorting, viscosity, gas buoyancy/diffusion, thermal phase changes, heat conduction, combustion, acid dilution, pressure propagation/obstacles, frame-rate independence, and inspection-view bounds. It also checks all 65 picker entries, new melting/freezing and dissolution reactions, germination, oxygen/CO2 behavior, fuses, all five bombs, timed and remote triggers, explosive gases, debris, cryogenic freezing, and an 800-charge edge-of-world chain. The mixed-material determinism test exercises every material through detonation. The sandbox and its tests have no Windows, Vulkan, or C++ dependency.

`RigidBodyTests` covers preset/custom placement, rejected overlaps, fixed bodies, falling/resting contacts, stacking, thin walls, rotation, grabbing, particle conservation, flotation, fragmentation, burning, explosive impulses, object limits, frame-rate independence, hollow-cup containment, melting, and the playground. It includes regressions for floating-point rasterization losing a one-pixel cup floor, sliding down both screen edges, sliding past object sides, falling off an overhang, reproducible connected silhouettes for 32 seeds of each natural preset, all 16 presets, slot-256 ownership, and thrown impact fracture (including grab release, gentle drops, fixed objects, boundaries, and a full fragment pool). `RigidBodyTests --gallery build/shape-gallery.ppm` exports a procedural asset contact sheet.

`tests/runtime_smoke.ps1` checks hidden-window startup, resize, minimized/restored size notifications, and clean shutdown on a Windows machine with a Vulkan 1.3 GPU. Add `-ObjectDemo` to exercise the workshop and a moving object scene:

```powershell
powershell -ExecutionPolicy Bypass -File tests/runtime_smoke.ps1 -Executable build/Debug/Omelette2D.exe
powershell -ExecutionPolicy Bypass -File tests/runtime_smoke.ps1 -Executable build/x64/Release/Omelette2D.exe -ObjectDemo
```

The C APIs use explicit ownership: pair `sandbox_create`/`sandbox_destroy` and `renderer_create`/`renderer_destroy`. Initialization returns `NULL` with a renderer error message on failure; drawing returns `false` on failure. Sprite views are owned by the sandbox and remain valid until the next sprite build or destruction.

**Custom PNG/XML sprites:** use **Browse XML...** in the Object workshop. See [SPRITES.md](SPRITES.md) for the format and `assets/sprites/example.xml` for a working example. Sprite artwork can assign multiple rigid materials and keeps its colors during rotation and destruction.
