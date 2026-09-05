cmake_minimum_required(VERSION 3.24)
project(Omelette2D LANGUAGES C CXX)

find_package(Vulkan REQUIRED)

# Pinned cimgui provides the C API for Dear ImGui and its backends.
add_library(OmeletteImGui STATIC
    third_party/cimgui/cimgui.cpp
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/imgui_demo.cpp
    third_party/imgui/backends/imgui_impl_win32.cpp
    third_party/imgui/backends/imgui_impl_vulkan.cpp
)
target_compile_features(OmeletteImGui PRIVATE cxx_std_11)
target_compile_definitions(OmeletteImGui PUBLIC CIMGUI_NO_EXPORT PRIVATE IMGUI_USER_CONFIG="backend_config.h")
target_include_directories(OmeletteImGui PUBLIC third_party/cimgui PRIVATE third_party/imgui)
target_compile_definitions(OmeletteImGui PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX VK_USE_PLATFORM_WIN32_KHR)
target_link_libraries(OmeletteImGui PUBLIC Vulkan::Vulkan)

add_executable(Omelette2D WIN32
    src/Ui.c
    src/Ui.h
    src/main.c
    src/VulkanRenderer.c
    src/VulkanRenderer.h
    src/Sandbox.c
    src/Shapes.c
    src/ShapeEditor.c
    src/ShapeEditor.h
    src/SpriteImport.c
    src/SpriteImport.h
    src/RigidBodies.inc
    src/Sandbox.h
    src/Materials.def
    src/Sprite.h
)
set_target_properties(Omelette2D PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
target_compile_definitions(Omelette2D PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX VK_USE_PLATFORM_WIN32_KHR UNICODE _UNICODE)
target_link_libraries(Omelette2D PRIVATE OmeletteImGui windowscodecs xmllite shlwapi ole32 uuid comdlg32)

include(CTest)
if(BUILD_TESTING)
    add_executable(SandboxTests tests/sandbox_regression.c src/Sandbox.c src/Shapes.c)
    target_include_directories(SandboxTests PRIVATE src)
    set_target_properties(SandboxTests PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
    add_executable(SpriteImportTests tests/sprite_import_regression.c src/SpriteImport.c src/Sandbox.c src/Shapes.c)
    target_include_directories(SpriteImportTests PRIVATE src)
    set_target_properties(SpriteImportTests PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
    target_link_libraries(SpriteImportTests PRIVATE windowscodecs xmllite shlwapi ole32 uuid)
    add_test(NAME SpriteImportRegression COMMAND SpriteImportTests "${CMAKE_CURRENT_SOURCE_DIR}/assets/sprites" "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/sprites")
    add_test(NAME SandboxRegression COMMAND SandboxTests)
    add_executable(RigidBodyTests tests/rigid_body_regression.c src/Sandbox.c src/Shapes.c)
    target_include_directories(RigidBodyTests PRIVATE src)
    set_target_properties(RigidBodyTests PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
    add_test(NAME RigidBodyRegression COMMAND RigidBodyTests)
endif()
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT Omelette2D)

find_program(GLSLC glslc HINTS "$ENV{VULKAN_SDK}/Bin" REQUIRED)
set(SHADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
foreach(shader sprite.vert sprite.frag)
    add_custom_command(
        OUTPUT "${SHADER_DIR}/${shader}.spv"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_DIR}"
        COMMAND ${GLSLC} "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader}" -o "${SHADER_DIR}/${shader}.spv"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader}"
        VERBATIM)
    list(APPEND SHADER_OUTPUTS "${SHADER_DIR}/${shader}.spv")
endforeach()
add_custom_target(OmeletteShaders ALL DEPENDS ${SHADER_OUTPUTS})
add_dependencies(Omelette2D OmeletteShaders)

add_custom_command(TARGET Omelette2D POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${SHADER_DIR}" "$<TARGET_FILE_DIR:Omelette2D>/shaders")
