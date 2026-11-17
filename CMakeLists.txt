cmake_minimum_required(VERSION 3.22)

project(rom-hamster
    VERSION 0.1.0
    DESCRIPTION "Windows 11 / Linux 双端 ROM 库管理器"
    LANGUAGES C CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# ---------------------------------------------------------------------------
# 选项（与 ARCHITECTURE.md §8.4 对齐）
# ---------------------------------------------------------------------------
option(RH_GUI        "构建 Qt 6 GUI（rom-hamster-gui）"        ON)
option(RH_MULTIMEDIA "启用 Qt Multimedia 内嵌视频（可选构建）" OFF)
set(RH_SANITIZE "" CACHE STRING "Sanitizer 列表，如 address,undefined（留空为关闭）")

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "构建类型" FORCE)
endif()

include(CTest)

if(RH_SANITIZE)
    add_compile_options(-fsanitize=${RH_SANITIZE} -fno-omit-frame-pointer)
    add_link_options(-fsanitize=${RH_SANITIZE})
endif()

# ---------------------------------------------------------------------------
# 依赖（§8.2 核心五库）
# ---------------------------------------------------------------------------
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB2 REQUIRED IMPORTED_TARGET glib-2.0>=2.66 gio-2.0>=2.66)
pkg_check_modules(SQLITE3 REQUIRED IMPORTED_TARGET sqlite3>=3.35)
pkg_check_modules(LIBARCHIVE REQUIRED IMPORTED_TARGET libarchive>=3.4)
pkg_check_modules(LIBXML2 REQUIRED IMPORTED_TARGET libxml-2.0>=2.9)
find_package(ZLIB REQUIRED)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/rh-version.h.in"
    "${CMAKE_BINARY_DIR}/generated/rh-version.h"
    @ONLY
)

# ---------------------------------------------------------------------------
# libromham（C11，禁止链接任何 GUI 工具包）
# ---------------------------------------------------------------------------
add_library(romham SHARED
    src/core/rh-err.c
    src/core/rh-ctx.c
    src/core/rh-path.c
    src/core/rh-str.c
    src/core/rh-records.c
    src/core/platform.c
    src/core/store.c
    src/core/store-schema.c
)

target_include_directories(romham
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/core>
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>
        $<INSTALL_INTERFACE:include/rom-hamster>
)

target_link_libraries(romham
    PRIVATE
        PkgConfig::GLIB2
        PkgConfig::SQLITE3
        PkgConfig::LIBARCHIVE
        PkgConfig::LIBXML2
        ZLIB::ZLIB
)

set_target_properties(romham PROPERTIES
    OUTPUT_NAME romham
    VERSION 1.0.0
    SOVERSION 1
)

if(MSVC)
    target_compile_options(romham PRIVATE /W4 /utf-8)
else()
    target_compile_options(romham PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
add_executable(rom-hamster src/cli/main.c)
target_link_libraries(rom-hamster PRIVATE romham PkgConfig::GLIB2)

if(MSVC)
    target_compile_options(rom-hamster PRIVATE /W4 /utf-8)
else()
    target_compile_options(rom-hamster PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# GUI（Qt 6 Widgets）
# ---------------------------------------------------------------------------
if(RH_GUI)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Network)
    if(RH_MULTIMEDIA)
        find_package(Qt6 6.8 REQUIRED COMPONENTS Multimedia)
    endif()

    qt_standard_project_setup()

    qt_add_executable(rom-hamster-gui
        src/gui/main.cpp
        resources/resources.qrc
    )

    target_link_libraries(rom-hamster-gui PRIVATE
        romham
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Network
    )

    if(RH_MULTIMEDIA)
        target_link_libraries(rom-hamster-gui PRIVATE Qt6::Multimedia)
        target_compile_definitions(rom-hamster-gui PRIVATE RH_MULTIMEDIA=1)
    endif()

    set_target_properties(rom-hamster-gui PROPERTIES
        WIN32_EXECUTABLE TRUE
        MACOSX_BUNDLE FALSE
    )
endif()

# ---------------------------------------------------------------------------
# 开发工具：从 256 PNG 生成 Windows ICO（纯 C，无外部依赖）
# ---------------------------------------------------------------------------
add_executable(make-ico tools/make-ico.c)
set(RH_ICO_OUT "${CMAKE_BINARY_DIR}/rom-hamster.ico")
add_custom_command(
    OUTPUT "${RH_ICO_OUT}"
    COMMAND make-ico "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/rom-hamster-256.png" "${RH_ICO_OUT}"
    DEPENDS make-ico "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/rom-hamster-256.png"
    COMMENT "生成 rom-hamster.ico"
    VERBATIM
)
add_custom_target(icon ALL DEPENDS "${RH_ICO_OUT}")

# ---------------------------------------------------------------------------
# 测试
# ---------------------------------------------------------------------------
if(BUILD_TESTING)
    add_executable(test-rh-err tests/unit/test-rh-err.c)
    target_link_libraries(test-rh-err PRIVATE romham PkgConfig::GLIB2)
    add_test(NAME core/rh-err COMMAND test-rh-err)

    add_executable(test-store tests/unit/test-store.c)
    target_link_libraries(test-store PRIVATE romham PkgConfig::GLIB2 PkgConfig::SQLITE3)
    add_test(NAME core/store COMMAND test-store)

    add_executable(test-path tests/unit/test-path.c)
    target_link_libraries(test-path PRIVATE romham PkgConfig::GLIB2)
    add_test(NAME core/path COMMAND test-path)

    add_executable(test-str tests/unit/test-str.c)
    target_link_libraries(test-str PRIVATE romham PkgConfig::GLIB2)
    add_test(NAME core/str COMMAND test-str)

    add_executable(test-records tests/unit/test-records.c)
    target_link_libraries(test-records PRIVATE romham PkgConfig::GLIB2)
    add_test(NAME core/records COMMAND test-records)

    add_executable(test-platform tests/unit/test-platform.c)
    target_link_libraries(test-platform PRIVATE romham PkgConfig::GLIB2)
    target_compile_definitions(test-platform PRIVATE
        RH_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
    add_test(NAME core/platform COMMAND test-platform)

    add_test(NAME core/no-gui-linkage
        COMMAND ${CMAKE_COMMAND}
            -DLIB=$<TARGET_FILE:romham>
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/check-no-gui-linkage.cmake"
    )

    add_test(NAME tools/make-ico
        COMMAND make-ico
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/rom-hamster-256.png"
            "${CMAKE_BINARY_DIR}/test-rom-hamster.ico"
    )
endif()

# ---------------------------------------------------------------------------
# 安装
# ---------------------------------------------------------------------------
include(GNUInstallDirs)

install(TARGETS rom-hamster RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# 出厂平台规则（只读，见 §10.2 第 2 层）
install(FILES data/platforms.conf
    DESTINATION "${CMAKE_INSTALL_DATADIR}/rom-hamster")

if(RH_GUI)
    install(TARGETS rom-hamster-gui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

    # 素材是 Qt 工程布局；安装时映射成 FDO hicolor，保证桌面能取到图标
    foreach(size 16 32 48 256)
        install(FILES "resources/icons/rom-hamster-${size}.png"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps"
            RENAME rom-hamster.png)
    endforeach()
    install(FILES "resources/icons/rom-hamster.svg"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps"
        RENAME rom-hamster.svg)
endif()
