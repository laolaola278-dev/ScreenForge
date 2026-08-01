# graphics：D3D11 设备管理与 GPU 检测（Phase 0）
add_library(screenforge_graphics STATIC
    D3D11Device.cpp
)

target_include_directories(screenforge_graphics PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(screenforge_graphics PUBLIC
    d3d11 dxgi
)

# 防御宏：避免 Windows.h 的 min/max 污染
target_compile_definitions(screenforge_graphics PUBLIC NOMINMAX)
