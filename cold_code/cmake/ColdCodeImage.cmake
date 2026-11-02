include_guard(GLOBAL)

# cold_code_add_image(<executable-target>
#                     SOURCES <cold sources...>
#                     [SLOT_SIZE <bytes>]
#                     [POOL_SLOTS <count>]
#                     [OUTPUT_DIR <dir>])
#
# Builds a flat cold image from the given sources (compiled with the cold-code
# restrictions), generates hot stubs/manifest, adds them to the target, and
# checks after linking that the slot pool landed in an RWX PT_LOAD.
#
# The helper is part of the cold_code library and locates its scripts/linker
# files relative to its own location, so the library can be consumed with
# add_subdirectory() without any knowledge of the consuming project.
#
# The consuming top-level project must enable ASM (generated stubs) and include
# this file via the library-provided COLD_CODE_HELPER cache variable.
#
# Map outputs in OUTPUT_DIR:
#   cold_image.map        - GNU ld map of the flat cold image
#   cold_code_layout.map  - generated function/slot/intra-offset table
#   <target>.map          - GNU ld map of the hot executable
#
# POOL_SLOTS is the number of physical slots kept resident (slot-level LRU
# capacity).  It is clamped to the total image slot count and must not exceed
# COLD_CODE_MAX_POOL_SLOTS (16).
#
# The default OUTPUT_DIR is shared by targets in the same directory.  If more
# than one target in a directory needs a cold image, pass distinct OUTPUT_DIR
# values (one cold image per executable in this version).
function(cold_code_add_image target)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    if(NOT CMAKE_ASM_COMPILER_LOADED)
        message(FATAL_ERROR
            "cold_code_add_image requires the ASM language; add ASM to the "
            "top-level project(... LANGUAGES ...) or call enable_language(ASM) there")
    endif()

    get_filename_component(cold_code_dir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
    set(include_dir "${cold_code_dir}/include")
    set(image_ld "${cold_code_dir}/linker/cold_image.ld")
    set(ram_ld "${cold_code_dir}/linker/cold_ram.ld")
    set(generator "${cold_code_dir}/tool/gen_cold_code.py")
    set(verifier "${cold_code_dir}/tool/verify_cold_ram.py")

    cmake_parse_arguments(ARG "" "SLOT_SIZE;POOL_SLOTS;OUTPUT_DIR" "SOURCES" ${ARGN})

    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "cold_code_add_image: SOURCES is required")
    endif()
    if(NOT ARG_SLOT_SIZE)
        set(ARG_SLOT_SIZE 4096)
    endif()
    if(NOT ARG_POOL_SLOTS)
        set(ARG_POOL_SLOTS 4)
    endif()
    if(ARG_POOL_SLOTS LESS 1 OR ARG_POOL_SLOTS GREATER 16)
        message(FATAL_ERROR "cold_code_add_image: POOL_SLOTS must be between 1 and 16")
    endif()
    if(NOT ARG_OUTPUT_DIR)
        set(ARG_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/cold_code_image")
    endif()

    set(objects "${target}_cold_image_objects")
    add_library(${objects} STATIC ${ARG_SOURCES})
    target_include_directories(${objects} PRIVATE "${include_dir}")
    target_compile_features(${objects} PRIVATE c_std_17)
    set_target_properties(${objects} PROPERTIES C_EXTENSIONS OFF)
    target_compile_options(${objects} PRIVATE
        -ffreestanding
        -fPIE
        -fvisibility=hidden
        -fno-stack-protector
        -fno-builtin
        -fno-asynchronous-unwind-tables
        -fno-unwind-tables
        -fno-jump-tables
        -fno-ident
        -mno-red-zone
        -ffunction-sections
        -fcf-protection=branch
        # -O1 keeps IPA-ICF disabled, so identical functions are not merged
        # into size-0 aliases (which would break stub generation).
        -O1
        -g0
        -DCOLD_CODE_BUILD_IMAGE
    )
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${objects} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Werror
        )
    endif()

    file(MAKE_DIRECTORY "${ARG_OUTPUT_DIR}")
    set(elf "${ARG_OUTPUT_DIR}/cold_image.elf")
    set(cold_map "${ARG_OUTPUT_DIR}/cold_image.map")
    set(raw_bin "${ARG_OUTPUT_DIR}/cold_code_raw.bin")
    set(final_bin "${ARG_OUTPUT_DIR}/cold_code.bin")
    set(gen_header "${ARG_OUTPUT_DIR}/cold_code_image.h")
    set(gen_source "${ARG_OUTPUT_DIR}/cold_code_image.c")
    set(gen_stubs "${ARG_OUTPUT_DIR}/cold_code_stubs.S")
    set(layout_map "${ARG_OUTPUT_DIR}/cold_code_layout.map")
    set(hot_map "${ARG_OUTPUT_DIR}/${target}.map")

    add_custom_command(
        OUTPUT "${elf}"
        BYPRODUCTS "${cold_map}"
        COMMAND "${CMAKE_C_COMPILER}" -nostdlib -no-pie -Wl,-e,0
                -Wl,--whole-archive "$<TARGET_FILE:${objects}>" -Wl,--no-whole-archive
                "-Wl,-T,${image_ld}" -Wl,--emit-relocs
                -Wl,--build-id=none "-Wl,-Map,${cold_map}"
                -o "${elf}"
        DEPENDS ${objects} "$<TARGET_FILE:${objects}>" "${image_ld}"
        COMMENT "Linking cold code image"
        VERBATIM
    )

    add_custom_command(
        OUTPUT "${raw_bin}"
        COMMAND "${CMAKE_OBJCOPY}" -O binary "${elf}" "${raw_bin}"
        DEPENDS "${elf}"
        COMMENT "Extracting cold code image"
        VERBATIM
    )

    add_custom_command(
        OUTPUT "${final_bin}" "${gen_header}" "${gen_source}" "${gen_stubs}"
               "${layout_map}"
        COMMAND "${Python3_EXECUTABLE}" "${generator}"
                --nm "${CMAKE_NM}"
                --readelf "${CMAKE_READELF}"
                --objdump "${CMAKE_OBJDUMP}"
                --elf "${elf}"
                --raw-bin "${raw_bin}"
                --final-bin "${final_bin}"
                --slot-size "${ARG_SLOT_SIZE}"
                --pool-slots "${ARG_POOL_SLOTS}"
                --image-header "${gen_header}"
                --image-source "${gen_source}"
                --stubs "${gen_stubs}"
                --layout-map "${layout_map}"
                --image-path "${final_bin}"
        DEPENDS "${elf}" "${raw_bin}" "${generator}"
        COMMENT "Generating cold code stubs and manifest"
        VERBATIM
    )

    target_sources(${target} PRIVATE "${gen_source}" "${gen_stubs}")
    target_include_directories(${target} PRIVATE "${ARG_OUTPUT_DIR}")
    set_source_files_properties("${gen_stubs}" PROPERTIES LANGUAGE ASM)
    add_custom_target("${target}_cold_image_gen" DEPENDS
        "${final_bin}" "${gen_header}" "${gen_source}" "${gen_stubs}" "${layout_map}")
    add_dependencies(${target} "${target}_cold_image_gen")

    target_link_options(${target} PRIVATE
        "-Wl,-T,${ram_ld}"
        "-Wl,--no-warn-rwx-segments"
        "-Wl,-Map,${hot_map}"
    )
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${ram_ld}")
    set_property(DIRECTORY APPEND PROPERTY ADDITIONAL_CLEAN_FILES "${hot_map}")

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${Python3_EXECUTABLE}" "${verifier}"
                --readelf "${CMAKE_READELF}"
                --elf "$<TARGET_FILE:${target}>"
                --symbol cold_code_slot_pool
        COMMENT "Verifying cold code RWX slot pool"
        VERBATIM
    )
endfunction()
