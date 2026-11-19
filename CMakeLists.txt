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
    src/core/rh-fname.c
    src/core/rh-hash.c
    src/core/rh-str.c
    src/core/rh-records.c
    src/core/platform.c
    src/core/library.c
    src/core/classify.c
    src/core/rh-archive.c
    src/core/dat.c
    src/core/import.c
    src/core/query.c
    src/core/detail.c
    src/core/manage.c
    src/core/export.c
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
add_executable(rom-hamster
    src/cli/main.c
    src/cli/cli-common.c
    src/cli/cmd-query.c
    src/cli/cmd-detail.c
    src/cli/cmd-manage.c
    src/cli/cmd-export.c
)
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

    add_executable(test-fname tests/unit/test-fname.c)
    target_link_libraries(test-fname PRIVATE romham PkgConfig::GLIB2)
    add_test(NAME core/fname COMMAND test-fname)

    add_executable(test-hash tests/unit/test-hash.c)
    target_link_libraries(test-hash PRIVATE romham PkgConfig::GLIB2)
    add_test(NAME core/hash COMMAND test-hash)

    add_executable(test-records tests/unit/test-records.c)
    target_link_libraries(test-records PRIVATE romham PkgConfig::GLIB2)
    add_test(NAME core/records COMMAND test-records)

    add_executable(test-platform tests/unit/test-platform.c)
    target_link_libraries(test-platform PRIVATE romham PkgConfig::GLIB2)
    target_compile_definitions(test-platform PRIVATE
        RH_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
    add_test(NAME core/platform COMMAND test-platform)

    add_executable(test-library tests/unit/test-library.c)
    target_link_libraries(test-library PRIVATE romham PkgConfig::GLIB2)
    add_test(NAME core/library COMMAND test-library)

    add_executable(test-classify tests/unit/test-classify.c)
    target_link_libraries(test-classify PRIVATE romham PkgConfig::GLIB2)
    target_compile_definitions(test-classify PRIVATE
        RH_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
    add_test(NAME core/classify COMMAND test-classify)

    add_executable(test-import tests/unit/test-import.c)
    target_link_libraries(test-import PRIVATE
        romham PkgConfig::GLIB2 PkgConfig::SQLITE3 PkgConfig::LIBARCHIVE)
    target_compile_definitions(test-import PRIVATE
        RH_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
    add_test(NAME core/import COMMAND test-import)

    add_executable(test-archive tests/unit/test-archive.c)
    target_link_libraries(test-archive PRIVATE
        romham PkgConfig::GLIB2 PkgConfig::LIBARCHIVE)
    target_compile_definitions(test-archive PRIVATE
        RH_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
        RH_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
    add_test(NAME core/archive COMMAND test-archive)

    add_executable(test-dat tests/unit/test-dat.c)
    target_link_libraries(test-dat PRIVATE
        romham PkgConfig::GLIB2 PkgConfig::SQLITE3)
    add_test(NAME core/dat COMMAND test-dat)

    add_executable(test-query tests/unit/test-query.c)
    target_link_libraries(test-query PRIVATE
        romham PkgConfig::GLIB2 PkgConfig::SQLITE3)
    add_test(NAME core/query COMMAND test-query)

    add_executable(test-detail tests/unit/test-detail.c)
    target_link_libraries(test-detail PRIVATE
        romham PkgConfig::GLIB2 PkgConfig::SQLITE3)
    add_test(NAME core/detail COMMAND test-detail)

    add_executable(test-manage tests/unit/test-manage.c)
    target_link_libraries(test-manage PRIVATE
        romham PkgConfig::GLIB2 PkgConfig::SQLITE3)
    add_test(NAME core/manage COMMAND test-manage)

    add_executable(test-export tests/unit/test-export.c)
    target_link_libraries(test-export PRIVATE
        romham PkgConfig::GLIB2 PkgConfig::SQLITE3)
    target_compile_definitions(test-export PRIVATE
        RH_TEST_FRONTEND_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/frontends"
        RH_TEST_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data/golden")
    add_test(NAME core/export COMMAND test-export)

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

# 核心库：与系统库目录隔离，避免与发行版 Qt/其他库冲突（§9.1）
install(TARGETS romham
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/rom-hamster"
        NAMELINK_SKIP
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
set_target_properties(rom-hamster PROPERTIES
    INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}/rom-hamster")

# 出厂平台规则（只读，见 §10.2 第 2 层）
install(FILES data/platforms.conf
    DESTINATION "${CMAKE_INSTALL_DATADIR}/rom-hamster")
install(DIRECTORY data/frontends
    DESTINATION "${CMAKE_INSTALL_DATADIR}/rom-hamster")

# /etc 下的系统配置与规则副本（conffile，§9.2）
install(FILES data/platforms.conf
    DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/rom-hamster")
install(FILES data/rom-hamster.conf.template
    DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/rom-hamster"
    RENAME rom-hamster.conf)

install(FILES data/rom-hamster.1
    DESTINATION "${CMAKE_INSTALL_MANDIR}/man1")

install(FILES LICENSE
    DESTINATION "${CMAKE_INSTALL_DATADIR}/rom-hamster")

# CLI 输出编码回归：在 C locale 下仍须是 UTF-8 字节（§15）
if(UNIX)
    add_test(NAME cli/version-utf8
        COMMAND ${CMAKE_COMMAND} -E env LC_ALL=C LANG=C
                $<TARGET_FILE:rom-hamster> --version)
    set_tests_properties(cli/version-utf8 PROPERTIES
        PASS_REGULAR_EXPRESSION "仓库:")

    # 伪终端回归：GLib 的 g_print/g_printerr 只在 stdout 是终端时才转码
    # （glib/gprint.c 的 g_fputs），只测管道会漏掉「TTY 下中文变 ?」。
    find_program(RH_SCRIPT_EXECUTABLE script)
    if(RH_SCRIPT_EXECUTABLE)
        add_test(NAME cli/version-utf8-tty
            COMMAND ${CMAKE_COMMAND} -E env LC_ALL=C LANG=C
                    ${RH_SCRIPT_EXECUTABLE} -q -c
                    "$<TARGET_FILE:rom-hamster> --version" /dev/null)
        set_tests_properties(cli/version-utf8-tty PROPERTIES
            PASS_REGULAR_EXPRESSION "仓库:")
    endif()
endif()

if(RH_GUI)
    install(TARGETS rom-hamster-gui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    set_target_properties(rom-hamster-gui PROPERTIES
        INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}/rom-hamster;$ORIGIN/../lib/rom-hamster/qt6")

    # 素材是 Qt 工程布局；安装时映射成 FDO hicolor，保证桌面能取到图标
    foreach(size 16 32 48 256)
        install(FILES "resources/icons/rom-hamster-${size}.png"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps"
            RENAME rom-hamster.png)
    endforeach()
    install(FILES "resources/icons/rom-hamster.svg"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps"
        RENAME rom-hamster.svg)

    install(FILES data/io.github.goddog312.RomHamster.desktop
        DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
    install(FILES data/io.github.goddog312.RomHamster.metainfo.xml
        DESTINATION "${CMAKE_INSTALL_DATADIR}/metainfo")
endif()
