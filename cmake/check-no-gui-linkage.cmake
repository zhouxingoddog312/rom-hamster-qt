# 断言：libromham 的链接依赖里不得出现 Qt / GTK（ARCHITECTURE.md §1.1 硬约束、§8.4）
if(NOT DEFINED LIB)
    message(FATAL_ERROR "需要 -DLIB=<libromham 路径>")
endif()

if(WIN32)
    message(STATUS "Windows 上由 dumpbin /dependents 在打包阶段检查，跳过 ldd")
    return()
endif()

find_program(RH_LDD ldd)
if(NOT RH_LDD)
    message(STATUS "未找到 ldd，跳过")
    return()
endif()

execute_process(
    COMMAND "${RH_LDD}" "${LIB}"
    OUTPUT_VARIABLE rh_ldd_out
    ERROR_VARIABLE rh_ldd_out
    RESULT_VARIABLE rh_ldd_rc
)
if(NOT rh_ldd_rc EQUAL 0)
    message(FATAL_ERROR "ldd 失败：${rh_ldd_out}")
endif()

string(REGEX MATCH "Qt6[A-Za-z]*|libgtk-[0-9]|libgdk-[0-9]|libpango-[0-9]" rh_bad "${rh_ldd_out}")
if(rh_bad)
    message(FATAL_ERROR "libromham 出现 GUI 工具包依赖：${rh_bad}\n${rh_ldd_out}")
endif()

message(STATUS "ok: libromham 不含 Qt/GTK 依赖")
