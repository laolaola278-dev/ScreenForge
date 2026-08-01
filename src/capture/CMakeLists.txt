# capture：WGC 屏幕捕获（Phase 1）
add_library(screenforge_capture STATIC
    WgcCaptureSource.cpp
)

target_include_directories(screenforge_capture PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(screenforge_capture PUBLIC
    screenforge_graphics
    d3d11 dxgi
)

# WinRT（Windows.Graphics.Capture）需要 windowsapp.lib
target_link_libraries(screenforge_capture PRIVATE windowsapp)

target_compile_definitions(screenforge_capture PUBLIC NOMINMAX)
target_compile_definitions(screenforge_capture PRIVATE WIN32_LEAN_AND_MEAN)
