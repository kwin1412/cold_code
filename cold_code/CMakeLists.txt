if(NOT CMAKE_ASM_COMPILER_LOADED)
    enable_language(ASM)
endif()

set(COLD_CODE_NEXT_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(COLD_CODE_NEXT_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/core")
set(COLD_CODE_NEXT_ARCH_X86_64_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/arch/x86_64")
set(COLD_CODE_NEXT_HOST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/port/host")

include(GNUInstallDirs)

add_library(cold_code_arch_x86_64 INTERFACE)
target_include_directories(cold_code_arch_x86_64 INTERFACE
    "$<BUILD_INTERFACE:${COLD_CODE_NEXT_ARCH_X86_64_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
set_target_properties(cold_code_arch_x86_64 PROPERTIES EXPORT_NAME arch_x86_64)

add_library(cold_code_core STATIC
    src/core/manager.c
    src/core/image.c
    src/core/storage.c
    src/core/source.c
    src/core/fault.c
    src/core/cold_code_internal.h
)
target_include_directories(cold_code_core PUBLIC
    "$<BUILD_INTERFACE:${COLD_CODE_NEXT_INCLUDE_DIR}>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_include_directories(cold_code_core PRIVATE "${COLD_CODE_NEXT_CORE_DIR}")
target_compile_features(cold_code_core PUBLIC c_std_17)
target_compile_options(cold_code_core PRIVATE
    -Wall -Wextra -Wpedantic -Wshadow -Werror
    -ffreestanding -fno-builtin -fno-stack-protector
    -fno-tree-loop-distribute-patterns
)
set_target_properties(cold_code_core PROPERTIES C_EXTENSIONS OFF EXPORT_NAME core)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    target_sources(cold_code_core PRIVATE src/arch/x86_64/abi_check.c)
    target_include_directories(cold_code_core PRIVATE
        "${COLD_CODE_NEXT_ARCH_X86_64_DIR}/include")
    target_link_libraries(cold_code_core PUBLIC cold_code_arch_x86_64)
endif()

# OBJECT (not STATIC) so the context-port definition is always present before
# the core archive is scanned.  This avoids static-link ordering cycles between
# the platform port and the core it serves.
add_library(cold_code_host OBJECT
    src/port/host/context_tls.c
    src/port/host/file_source.c
)
target_include_directories(cold_code_host PUBLIC
    "$<BUILD_INTERFACE:${COLD_CODE_NEXT_HOST_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_link_libraries(cold_code_host PUBLIC cold_code_core)
set_target_properties(cold_code_host PROPERTIES EXPORT_NAME host)
target_compile_options(cold_code_host PRIVATE
    -Wall -Wextra -Wpedantic -Wshadow -Werror
    -ftls-model=local-exec
)
target_compile_features(cold_code_host PUBLIC c_std_17)
set_target_properties(cold_code_host PROPERTIES C_EXTENSIONS OFF)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    target_sources(cold_code_host PRIVATE src/port/host/fast_x86_64.S)
endif()

# Per-core reference port.  OBJECT so its context-port definition is always
# present before the core archive is scanned.
add_library(cold_code_baremetal OBJECT
    src/port/baremetal/context_per_core.c
)
target_include_directories(cold_code_baremetal PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/port/baremetal/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_link_libraries(cold_code_baremetal PUBLIC cold_code_core)
set_target_properties(cold_code_baremetal PROPERTIES EXPORT_NAME baremetal)
target_compile_options(cold_code_baremetal PRIVATE
    -Wall -Wextra -Wpedantic -Wshadow -Werror
    -ffreestanding -fno-builtin -fno-stack-protector
    -fno-tree-loop-distribute-patterns
)
target_compile_features(cold_code_baremetal PUBLIC c_std_17)
set_target_properties(cold_code_baremetal PROPERTIES C_EXTENSIONS OFF)

add_library(cold_code::core ALIAS cold_code_core)
add_library(cold_code::host ALIAS cold_code_host)
add_library(cold_code::baremetal ALIAS cold_code_baremetal)
add_library(cold_code::arch_x86_64 ALIAS cold_code_arch_x86_64)

set(COLD_CODE_HELPER "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ColdCodeImage.cmake"
    CACHE INTERNAL "Path to the cold_code_add_image() CMake helper")
set(COLD_CODE_TOOL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tool"
    CACHE INTERNAL "Path to cold_code generator tools")
set(COLD_CODE_LINKER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/linker"
    CACHE INTERNAL "Path to cold_code linker scripts")

include(CMakePackageConfigHelpers)

install(TARGETS cold_code_core cold_code_arch_x86_64
    EXPORT cold_codeTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

install(TARGETS cold_code_host cold_code_baremetal
    EXPORT cold_codeTargets
    OBJECTS DESTINATION "${CMAKE_INSTALL_LIBDIR}/cold_code"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(DIRECTORY src/arch/x86_64/include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(DIRECTORY src/port/host/include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(DIRECTORY src/port/baremetal/include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(DIRECTORY tool DESTINATION "${CMAKE_INSTALL_DATADIR}/cold_code")
install(DIRECTORY linker DESTINATION "${CMAKE_INSTALL_DATADIR}/cold_code")
install(FILES cmake/ColdCodeImage.cmake
    DESTINATION "${CMAKE_INSTALL_DATADIR}/cold_code/cmake")

configure_package_config_file(
    cmake/cold_code-config.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/cold_code-config.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/cold_code"
    PATH_VARS CMAKE_INSTALL_INCLUDEDIR CMAKE_INSTALL_DATADIR)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/cold_code-config-version.cmake"
    VERSION 3.0.0
    COMPATIBILITY SameMajorVersion)
install(EXPORT cold_codeTargets
    NAMESPACE cold_code::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/cold_code")
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/cold_code-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/cold_code-config-version.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/cold_code")
