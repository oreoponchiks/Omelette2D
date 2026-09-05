# Vendored cimgui

Source: https://github.com/cimgui/cimgui

Pinned revision: `d298666861ebf00dcfeb2407409931c04e47e33c` (Dear ImGui **1.92.8**, non-docking).

`cimgui.cpp`, `cimgui.h`, `cimgui_impl.h`, and `LICENSE` are upstream files. The generated wrapper includes are adjusted from `./imgui/` to `../imgui/` to use this project's existing Dear ImGui copy. No API or generated struct layout is changed.

`backend_config.h` gives the existing Vulkan and Win32 backends C linkage, following cimgui's backend build instructions. `cimgui_impl_win32.h` supplies the four Win32 declarations used by the engine, since the upstream generated backend header does not include Win32. Two redundant `extern` prefixes in `../imgui/backends/imgui_impl_win32.cpp` are removed so `IMGUI_IMPL_API=extern "C"` compiles correctly.

The library is linked statically with `CIMGUI_NO_EXPORT`. `IMGUI_DISABLE_OBSOLETE_FUNCTIONS` matches upstream cimgui's generator/build configuration and removes the obsolete overloaded Vulkan texture function, which cannot use C linkage. Engine code includes the generated C declarations and calls cimgui directly; `src/Ui.c` owns context/backend initialization and shutdown. Initialization verifies the generated core struct sizes against the linked Dear ImGui library. cimgui and Dear ImGui remain C++ dependencies.
