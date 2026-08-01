cmake_minimum_required(VERSION 3.24)
project(ScreenForge VERSION 0.8.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── 构建模式（Phase 7-B） ──
# 默认：NVENC SDK 缺失时自动跳过硬件编码器 → SIMULATION-only 构建（NvencSimulator 回退）
# 设置 -DSF_REQUIRE_NVENC=ON 可强制要求 SDK（缺失即 FATAL_ERROR）
option(SF_REQUIRE_NVENC "Require NVIDIA Video Codec SDK (hard fail if missing)" OFF)

# 唯一 UI 依赖：Qt6 Widgets
find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_subdirectory(src)
