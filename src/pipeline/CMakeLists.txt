# pipeline：GPU 帧管线（Phase 2）
# SPSC FrameQueue + QPC Pacing 消费线程（60fps）
add_library(screenforge_pipeline STATIC
    FramePipeline.cpp
)

target_include_directories(screenforge_pipeline PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(screenforge_pipeline PUBLIC
    screenforge_capture
    screenforge_graphics
    d3d11 dxgi
)

target_compile_definitions(screenforge_pipeline PUBLIC NOMINMAX)
