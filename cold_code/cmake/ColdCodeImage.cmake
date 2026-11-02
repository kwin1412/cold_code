include_guard(GLOBAL)

# Generated stubs are assembly; enable ASM automatically.  This file must be
# included at directory scope (not from inside a function).
if(NOT CMAKE_ASM_COMPILER_LOADED)
    enable_language(ASM)
endif()

# cold_code_add_image(<executable-target>
#   NAME <identifier>
#   SOURCES <cold sources...>
#   [SLOT_SIZE <bytes>] [POOL_SLOTS <max>] [ARCH <name>]
#   [STUBS SLOW|FAST] [FAST_PATH ON|OFF]   # FAST_PATH is a compatibility alias
#   [OUTPUT_DIR <dir>] [RAM_SECTION ON|OFF] [VERIFY_RWX ON|OFF]
#   [DEFINE_PATH ON|OFF] [COLD_FLAGS <flags...>]
# )
function(cold_code_add_image target)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)

    foreach(tool_name OBJCOPY NM READELF OBJDUMP)
        if(NOT CMAKE_${tool_name})
            message(FATAL_ERROR
                "cold_code_add_image: CMAKE_${tool_name} is not set; a GNU/LLVM "
                "binutils-compatible toolchain is required")
        endif()
    endforeach()

    get_filename_component(cold_code_dir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
    if(NOT COLD_CODE_INCLUDE_DIR)
        set(COLD_CODE_INCLUDE_DIR "${cold_code_dir}/include")
    endif()
    if(NOT COLD_CODE_TOOL_DIR)
        set(COLD_CODE_TOOL_DIR "${cold_code_dir}/tool")
    endif()
    if(NOT COLD_CODE_LINKER_DIR)
        set(COLD_CODE_LINKER_DIR "${cold_code_dir}/linker")
    endif()
    set(include_dir "${COLD_CODE_INCLUDE_DIR}")
    set(generator "${COLD_CODE_TOOL_DIR}/gen_cold_code.py")
    set(verifier "${COLD_CODE_TOOL_DIR}/host/verify_rwx.py")
    set(duplicate_checker "${COLD_CODE_TOOL_DIR}/check_duplicate_symbols.py")

    cmake_parse_arguments(ARG ""
        "NAME;SLOT_SIZE;POOL_SLOTS;ARCH;STUBS;FAST_PATH;OUTPUT_DIR;RAM_SECTION;VERIFY_RWX;DEFINE_PATH"
        "SOURCES;COLD_FLAGS" ${ARGN})
    if(ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "cold_code_add_image: unknown arguments: ${ARG_UNPARSED_ARGUMENTS}")
    endif()

    if(NOT ARG_NAME)
        message(FATAL_ERROR "cold_code_add_image: NAME is required")
    endif()
    if(NOT ARG_NAME MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
        message(FATAL_ERROR
            "cold_code_add_image: NAME '${ARG_NAME}' must be a C identifier")
    endif()
    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "cold_code_add_image: SOURCES is required")
    endif()

    get_target_property(target_type ${target} TYPE)
    if(NOT target_type STREQUAL "EXECUTABLE")
        message(FATAL_ERROR "cold_code_add_image: ${target} must be an executable target")
    endif()

    if(NOT ARG_ARCH)
        string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" processor)
        if(processor MATCHES "^(x86_64|amd64)$")
            set(ARG_ARCH x86_64)
        elseif(processor MATCHES "^(armv7m|arm)$")
            set(ARG_ARCH armv7m)
        elseif(processor MATCHES "^(aarch64|arm64)$")
            set(ARG_ARCH aarch64)
        elseif(processor MATCHES "^riscv64$")
            set(ARG_ARCH riscv64)
        elseif(processor MATCHES "^riscv32$")
            set(ARG_ARCH riscv32)
        else()
            message(FATAL_ERROR
                "cold_code_add_image: cannot infer ARCH from '${CMAKE_SYSTEM_PROCESSOR}'; "
                "pass ARCH explicitly")
        endif()
    endif()

    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${generator}" --arch "${ARG_ARCH}" --print-arch-info
        OUTPUT_VARIABLE arch_info
        ERROR_VARIABLE arch_error
        RESULT_VARIABLE arch_result
    )
    if(NOT arch_result EQUAL 0)
        message(FATAL_ERROR "cold_code_add_image: ${arch_error}")
    endif()
    string(REPLACE "\r" "" arch_info "${arch_info}")
    string(REPLACE "\n" ";" arch_lines "${arch_info}")
    foreach(line IN LISTS arch_lines)
        if(line MATCHES "^([A-Za-z0-9_]+)=(.*)$")
            set("backend_${CMAKE_MATCH_1}" "${CMAKE_MATCH_2}")
        endif()
    endforeach()
    if(NOT backend_pool_alignment)
        message(FATAL_ERROR "cold_code_add_image: backend did not report pool_alignment")
    endif()

    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${generator}" --arch "${ARG_ARCH}" --print-cflags
        OUTPUT_VARIABLE cflags_text
        ERROR_VARIABLE cflags_error
        RESULT_VARIABLE cflags_result
    )
    if(NOT cflags_result EQUAL 0)
        message(FATAL_ERROR "cold_code_add_image: ${cflags_error}")
    endif()
    separate_arguments(backend_cflags UNIX_COMMAND "${cflags_text}")

    set(stubs_mode "${ARG_STUBS}")
    if(ARG_FAST_PATH)
        string(TOUPPER "${ARG_FAST_PATH}" fast_path_upper)
        if(fast_path_upper STREQUAL "ON")
            set(fast_alias FAST)
        elseif(fast_path_upper STREQUAL "OFF")
            set(fast_alias SLOW)
        else()
            message(FATAL_ERROR "cold_code_add_image: FAST_PATH must be ON or OFF")
        endif()
        if(stubs_mode AND NOT stubs_mode STREQUAL fast_alias)
            message(FATAL_ERROR
                "cold_code_add_image: STUBS and FAST_PATH disagree for ${ARG_NAME}")
        endif()
        set(stubs_mode "${fast_alias}")
    endif()
    if(NOT stubs_mode)
        set(stubs_mode SLOW)
    endif()
    string(TOUPPER "${stubs_mode}" stubs_upper)
    if(stubs_upper STREQUAL "FAST")
        set(stubs_mode fast)
        if(NOT backend_supports_fast)
            message(FATAL_ERROR
                "cold_code_add_image: arch '${ARG_ARCH}' does not support fast stubs")
        endif()
        if(NOT backend_fast_accessor)
            message(FATAL_ERROR "cold_code_add_image: backend did not report a fast accessor")
        endif()
    elseif(stubs_upper STREQUAL "SLOW")
        set(stubs_mode slow)
        if(NOT backend_supports_slow)
            message(FATAL_ERROR
                "cold_code_add_image: arch '${ARG_ARCH}' does not support slow stubs")
        endif()
    else()
        message(FATAL_ERROR "cold_code_add_image: STUBS must be SLOW or FAST")
    endif()

    if(NOT ARG_SLOT_SIZE)
        set(ARG_SLOT_SIZE 4096)
    endif()
    if(ARG_SLOT_SIZE LESS 1)
        message(FATAL_ERROR "cold_code_add_image: SLOT_SIZE must be positive")
    endif()
    math(EXPR slot_remainder "${ARG_SLOT_SIZE} % ${backend_pool_alignment}")
    if(NOT slot_remainder EQUAL 0)
        message(FATAL_ERROR
            "cold_code_add_image: SLOT_SIZE ${ARG_SLOT_SIZE} must be a multiple of the "
            "${ARG_ARCH} pool alignment ${backend_pool_alignment}")
    endif()

    if(NOT ARG_POOL_SLOTS)
        set(ARG_POOL_SLOTS 4)
    endif()
    if(ARG_POOL_SLOTS LESS 1 OR ARG_POOL_SLOTS GREATER 16)
        message(FATAL_ERROR "cold_code_add_image: POOL_SLOTS must be between 1 and 16")
    endif()

    string(TOLOWER "${ARG_NAME}" image_lower)
    get_target_property(image_names_lower ${target} COLD_CODE_IMAGE_NAMES_LOWER)
    if(image_names_lower AND image_lower IN_LIST image_names_lower)
        message(FATAL_ERROR
            "cold_code_add_image: image '${ARG_NAME}' is already defined for ${target}")
    endif()
    set_property(TARGET ${target} APPEND PROPERTY COLD_CODE_IMAGE_NAMES_LOWER "${image_lower}")

    if(NOT ARG_OUTPUT_DIR)
        set(ARG_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/cold_code_${target}_${ARG_NAME}")
    endif()
    get_filename_component(ARG_OUTPUT_DIR "${ARG_OUTPUT_DIR}" ABSOLUTE
        BASE_DIR "${CMAKE_CURRENT_BINARY_DIR}")
    file(MAKE_DIRECTORY "${ARG_OUTPUT_DIR}")

    string(TOUPPER "${ARG_NAME}" image_upper)
    set(objects "${target}_${ARG_NAME}_cold_image_objects")
    set(image_ld "${COLD_CODE_LINKER_DIR}/${ARG_ARCH}/cold_image.ld")
    set(ram_ld "${COLD_CODE_LINKER_DIR}/${ARG_ARCH}/cold_ram_host.ld")
    if(COLD_CODE_ARCH_INCLUDE_DIR)
        set(arch_include_dir "${COLD_CODE_ARCH_INCLUDE_DIR}")
    else()
        set(arch_include_dir "${cold_code_dir}/src/arch/${ARG_ARCH}/include")
    endif()
    if(NOT EXISTS "${image_ld}")
        message(FATAL_ERROR "cold_code_add_image: missing linker script ${image_ld}")
    endif()

    add_library(${objects} STATIC ${ARG_SOURCES})
    target_include_directories(${objects} PRIVATE "${include_dir}")
    target_compile_features(${objects} PRIVATE c_std_17)
    set_target_properties(${objects} PROPERTIES C_EXTENSIONS OFF)
    target_compile_options(${objects} PRIVATE
        ${backend_cflags}
        -O1
        -g0
        "-DCOLD_CODE_BUILD_IMAGE"
        "-DCOLD_CODE_IMAGE_SLOT_SIZE=${ARG_SLOT_SIZE}"
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Werror
        ${ARG_COLD_FLAGS}
    )

    set(elf "${ARG_OUTPUT_DIR}/cold_image_${ARG_NAME}.elf")
    set(cold_map "${ARG_OUTPUT_DIR}/cold_image_${ARG_NAME}.map")
    set(raw_bin "${ARG_OUTPUT_DIR}/cold_code_${ARG_NAME}_raw.bin")
    set(final_bin "${ARG_OUTPUT_DIR}/cold_code_${ARG_NAME}.bin")
    set(gen_header "${ARG_OUTPUT_DIR}/cold_code_${ARG_NAME}_image.h")
    set(gen_source "${ARG_OUTPUT_DIR}/cold_code_${ARG_NAME}_image.c")
    set(gen_stubs "${ARG_OUTPUT_DIR}/cold_code_${ARG_NAME}_stubs.S")
    set(layout_map "${ARG_OUTPUT_DIR}/cold_code_${ARG_NAME}_layout.map")
    set(symbols "${ARG_OUTPUT_DIR}/cold_code_${ARG_NAME}_symbols.txt")
    set(hot_map "${CMAKE_CURRENT_BINARY_DIR}/${target}.map")

    add_custom_command(
        OUTPUT "${elf}"
        BYPRODUCTS "${cold_map}"
        COMMAND "${CMAKE_C_COMPILER}" -nostdlib -no-pie -Wl,-e,0
                -Wl,--whole-archive "$<TARGET_FILE:${objects}>" -Wl,--no-whole-archive
                "-Wl,-T,${image_ld}" -Wl,--emit-relocs
                -Wl,--build-id=none "-Wl,-Map,${cold_map}"
                -o "${elf}"
        DEPENDS ${objects} "$<TARGET_FILE:${objects}>" "${image_ld}"
        COMMENT "Linking cold code image ${ARG_NAME}"
        VERBATIM
    )

    add_custom_command(
        OUTPUT "${raw_bin}"
        COMMAND "${CMAKE_OBJCOPY}" -O binary "${elf}" "${raw_bin}"
        DEPENDS "${elf}"
        COMMENT "Extracting cold code payload ${ARG_NAME}"
        VERBATIM
    )

    file(GLOB arch_py_files "${COLD_CODE_TOOL_DIR}/arch/*.py")
    file(GLOB public_headers "${COLD_CODE_INCLUDE_DIR}/cold_code.h"
        "${COLD_CODE_INCLUDE_DIR}/cold_code/*.h" "${COLD_CODE_INCLUDE_DIR}/cold_code/*/*.h")
    set(generator_deps "${elf}" "${raw_bin}" "${generator}" ${arch_py_files} ${public_headers})
    if(stubs_mode STREQUAL "fast")
        list(APPEND generator_deps
            "${arch_include_dir}/cold_code/abi/v1/${ARG_ARCH}.h")
    endif()

    set(generator_args
        --arch "${ARG_ARCH}"
        --stubs-mode "${stubs_mode}"
        --nm "${CMAKE_NM}"
        --readelf "${CMAKE_READELF}"
        --objdump "${CMAKE_OBJDUMP}"
        --elf "${elf}"
        --raw-bin "${raw_bin}"
        --final-bin "${final_bin}"
        --image-name "${ARG_NAME}"
        --slot-size "${ARG_SLOT_SIZE}"
        --pool-slots "${ARG_POOL_SLOTS}"
        --image-header "${gen_header}"
        --image-source "${gen_source}"
        --stubs "${gen_stubs}"
        --layout-map "${layout_map}"
        --symbols "${symbols}"
    )
    if(stubs_mode STREQUAL "fast")
        list(APPEND generator_args --fast-accessor "${backend_fast_accessor}")
    endif()

    add_custom_command(
        OUTPUT "${final_bin}" "${gen_header}" "${gen_source}" "${gen_stubs}"
               "${layout_map}" "${symbols}"
        COMMAND "${Python3_EXECUTABLE}" "${generator}" ${generator_args}
        DEPENDS ${generator_deps}
        COMMENT "Generating cold code stubs and image ${ARG_NAME}"
        VERBATIM
    )

    target_sources(${target} PRIVATE "${gen_source}" "${gen_stubs}" "${gen_header}")
    target_include_directories(${target} PRIVATE "${ARG_OUTPUT_DIR}")
    if(EXISTS "${arch_include_dir}")
        target_include_directories(${target} PRIVATE "${arch_include_dir}")
    endif()
    set_source_files_properties("${gen_stubs}" PROPERTIES LANGUAGE ASM)
    target_link_libraries(${target} PRIVATE cold_code::core)
    if(ARG_DEFINE_PATH)
        string(TOUPPER "${ARG_DEFINE_PATH}" define_path_upper)
        if(define_path_upper STREQUAL "ON")
            target_compile_definitions(${target} PRIVATE
                "COLD_CODE_IMAGE_${image_upper}_PATH=\"${final_bin}\"")
        elseif(NOT define_path_upper STREQUAL "OFF")
            message(FATAL_ERROR "cold_code_add_image: DEFINE_PATH must be ON or OFF")
        endif()
    endif()

    set(gen_target "${target}_cold_code_gen_${ARG_NAME}")
    add_custom_target(${gen_target} DEPENDS
        "${final_bin}" "${gen_header}" "${gen_source}" "${gen_stubs}" "${layout_map}"
        "${symbols}")
    add_dependencies(${target} "${gen_target}")

    set(ram_section_on TRUE)
    if(ARG_RAM_SECTION)
        string(TOUPPER "${ARG_RAM_SECTION}" ram_upper)
        if(ram_upper STREQUAL "ON")
            set(ram_section_on TRUE)
        elseif(ram_upper STREQUAL "OFF")
            set(ram_section_on FALSE)
        else()
            message(FATAL_ERROR "cold_code_add_image: RAM_SECTION must be ON or OFF")
        endif()
    endif()

    get_target_property(cold_link_applied ${target} COLD_CODE_LINK_APPLIED)
    if(ram_section_on AND NOT cold_link_applied)
        target_link_options(${target} PRIVATE
            "-Wl,-T,${ram_ld}"
            "-Wl,--no-warn-rwx-segments"
            "-Wl,-Map,${hot_map}"
        )
        set_property(TARGET ${target} PROPERTY COLD_CODE_LINK_APPLIED TRUE)
        set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${ram_ld}")
        set_property(DIRECTORY APPEND PROPERTY ADDITIONAL_CLEAN_FILES "${hot_map}")
    endif()

    set(verify_rwx_on ${ram_section_on})
    if(ARG_VERIFY_RWX)
        string(TOUPPER "${ARG_VERIFY_RWX}" verify_upper)
        if(verify_upper STREQUAL "ON")
            set(verify_rwx_on TRUE)
        elseif(verify_upper STREQUAL "OFF")
            set(verify_rwx_on FALSE)
        else()
            message(FATAL_ERROR "cold_code_add_image: VERIFY_RWX must be ON or OFF")
        endif()
    endif()
    if(verify_rwx_on)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${Python3_EXECUTABLE}" "${verifier}"
                    --readelf "${CMAKE_READELF}"
                    --elf "$<TARGET_FILE:${target}>"
                    --symbol "cold_code_ram_marker_${ARG_NAME}"
                    --prefix "cold_code_storage_marker_${ARG_NAME}_"
            COMMENT "Verifying cold code RWX storage (${ARG_NAME})"
            VERBATIM
        )
    endif()

    set_property(TARGET ${target} APPEND PROPERTY COLD_CODE_SYMBOL_FILES "${symbols}")
    get_target_property(all_symbols ${target} COLD_CODE_SYMBOL_FILES)
    set_property(TARGET ${target} APPEND PROPERTY COLD_CODE_GEN_TARGETS "${gen_target}")
    get_target_property(all_gen_targets ${target} COLD_CODE_GEN_TARGETS)
    set(check_target "${target}_cold_code_symbols_${ARG_NAME}")
    add_custom_target(${check_target}
        COMMAND "${Python3_EXECUTABLE}" "${duplicate_checker}" --symbols ${all_symbols}
        COMMENT "Checking cold function names for ${ARG_NAME}"
        VERBATIM
    )
    add_dependencies(${check_target} ${all_gen_targets})
    add_dependencies(${target} "${check_target}")
endfunction()

set(COLD_CODE_HELPER "${CMAKE_CURRENT_LIST_DIR}/ColdCodeImage.cmake"
    CACHE INTERNAL "Path to the cold_code_add_image() CMake helper")
set(COLD_CODE_TOOL_DIR "${CMAKE_CURRENT_LIST_DIR}/../tool"
    CACHE INTERNAL "Path to cold_code generator tools")
set(COLD_CODE_LINKER_DIR "${CMAKE_CURRENT_LIST_DIR}/../linker"
    CACHE INTERNAL "Path to cold_code linker scripts")
