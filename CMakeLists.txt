cmake_minimum_required(VERSION 3.10)
project(Formidable2026)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# 强制使用 UTF-8 编码，防止中文乱码和编码识别错误
if(MSVC)
    add_compile_options(/utf-8)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS -D_WINSOCK_DEPRECATED_NO_WARNINGS)
endif()
# 设置输出目录为根目录下的 Formidable2026 (x64/x86) 结构
if(MSVC)
    # 针对 Visual Studio，我们将输出目录固定到根目录的 Formidable2026
    # 使用宏 $(Platform) 和 $(Configuration) 以支持 IDE 内的架构/配置切换
    set(OUTPUT_DIR "${CMAKE_SOURCE_DIR}/Formidable2026/$(Platform)/$(Configuration)")
    
    # 全局设置输出目录
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_DIR}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${OUTPUT_DIR}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${OUTPUT_DIR}")
    
    # 强制覆盖所有配置的输出目录（防止 CMake 默认行为干扰）
    foreach(OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES})
        string(TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG_UPPER)
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG_UPPER} "${OUTPUT_DIR}")
        set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${OUTPUTCONFIG_UPPER} "${OUTPUT_DIR}")
        set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${OUTPUTCONFIG_UPPER} "${OUTPUT_DIR}")
    endforeach()
else()
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(ARCH "x64")
    else()
        set(ARCH "x86")
    endif()
    set(OUTPUT_BASE_DIR "${CMAKE_SOURCE_DIR}/Formidable2026/${ARCH}")
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_BASE_DIR}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${OUTPUT_BASE_DIR}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${OUTPUT_BASE_DIR}")
endif()
# 包含路径
include_directories(${CMAKE_SOURCE_DIR})
# Common 库 (Interface)
add_library(Common INTERFACE)
target_include_directories(Common INTERFACE ${CMAKE_SOURCE_DIR}/Common)
# 主控端 (GUI版) - 重命名为 Formidable2026
set(MASTER_GUI_SRCS 
    Master/main_gui.cpp 
    Master/MasterGUI.rc
)
add_executable(Formidable2026 ${MASTER_GUI_SRCS})
target_link_libraries(Formidable2026 Common ws2_32 comctl32 shell32)
if(MSVC)
    set_target_properties(Formidable2026 PROPERTIES LINK_FLAGS "/SUBSYSTEM:WINDOWS")
    set_target_properties(Formidable2026 PROPERTIES OUTPUT_NAME "Formidable2026")
    
    # 核心修复：确保资源编译器 (RC) 能找到生成的二进制文件
    # 我们将 DLL 和 Client 的输出路径添加到 RC 的包含搜索路径中
    # 使用 $(Platform) 和 $(Configuration) 宏，RC 会在当前正在编译的架构目录下查找
    target_compile_options(Formidable2026 PRIVATE 
        "$<$<COMPILE_LANGUAGE:RC>:/I\"${CMAKE_SOURCE_DIR}/Formidable2026/$(Platform)/$(Configuration)\">"
    )
endif()
# 被控端
add_executable(Client Client/main.cpp)
target_link_libraries(Client Common ws2_32 advapi32 user32)
# 设置 Client 为窗口程序（隐藏控制台，仅在发布版生效）
if(MSVC)
    set_target_properties(Client PROPERTIES LINK_FLAGS_RELEASE "/SUBSYSTEM:WINDOWS")
    set_target_properties(Client PROPERTIES LINK_FLAGS_MINSIZEREL "/SUBSYSTEM:WINDOWS")
    set_target_properties(Client PROPERTIES LINK_FLAGS_RELWITHDEBINFO "/SUBSYSTEM:WINDOWS")
endif()
# 功能模块 (DLL)
add_library(ModuleSystemInfo SHARED Modules/SystemInfo/SystemInfo.cpp)
target_link_libraries(ModuleSystemInfo Common ws2_32 ntdll)
set_target_properties(ModuleSystemInfo PROPERTIES OUTPUT_NAME "SystemInfo")
add_library(ModuleFileManager SHARED Modules/FileManager/FileManager.cpp)
target_link_libraries(ModuleFileManager Common ws2_32)
set_target_properties(ModuleFileManager PROPERTIES OUTPUT_NAME "FileManager")
add_library(ModuleProcessManager SHARED Modules/ProcessManager/ProcessManager.cpp)
target_link_libraries(ModuleProcessManager Common ws2_32)
set_target_properties(ModuleProcessManager PROPERTIES OUTPUT_NAME "ProcessManager")
add_library(ModuleTerminal SHARED Modules/Terminal/Terminal.cpp)
target_link_libraries(ModuleTerminal Common ws2_32)
set_target_properties(ModuleTerminal PROPERTIES OUTPUT_NAME "Terminal")
add_library(ModuleServiceManager SHARED Modules/ServiceManager/ServiceManager.cpp)
target_link_libraries(ModuleServiceManager Common ws2_32 advapi32)
set_target_properties(ModuleServiceManager PROPERTIES OUTPUT_NAME "ServiceManager")
add_library(ModuleWindowManager SHARED Modules/WindowManager/WindowManager.cpp)
target_link_libraries(ModuleWindowManager Common ws2_32 user32)
set_target_properties(ModuleWindowManager PROPERTIES OUTPUT_NAME "WindowManager")
add_library(ModuleRegistryManager SHARED Modules/RegistryManager/RegistryManager.cpp)
target_link_libraries(ModuleRegistryManager Common ws2_32 advapi32)
set_target_properties(ModuleRegistryManager PROPERTIES OUTPUT_NAME "RegistryManager")
add_library(ModuleMultimedia SHARED Modules/Multimedia/Multimedia.cpp)
target_link_libraries(ModuleMultimedia Common ws2_32)
set_target_properties(ModuleMultimedia PROPERTIES OUTPUT_NAME "Multimedia")
# 设置主控端的显式依赖，确保资源嵌入前，DLL和Client已生成
add_dependencies(Formidable2026 
    Client 
    ModuleSystemInfo 
    ModuleFileManager 
    ModuleProcessManager 
    ModuleTerminal 
    ModuleServiceManager 
    ModuleWindowManager 
    ModuleRegistryManager 
    ModuleMultimedia
)
