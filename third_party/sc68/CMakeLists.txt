find_package(ZLIB REQUIRED)

function(disable_sc68_sanitizers target_name)
    target_compile_options(${target_name} PRIVATE
        $<$<CONFIG:Debug>:-fno-sanitize=all>
    )
endfunction()

add_library(sc68_unice68 STATIC
    unice68/unice68_pack.c
    unice68/unice68_unpack.c
    unice68/unice68_version.c
)
target_include_directories(sc68_unice68 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/unice68
)
target_compile_definitions(sc68_unice68 PRIVATE HAVE_CONFIG_H)
target_compile_features(sc68_unice68 PRIVATE c_std_99)
disable_sc68_sanitizers(sc68_unice68)

add_library(sc68_file68 STATIC
    file68/src/error68.c
    file68/src/file68.c
    file68/src/gzip68.c
    file68/src/ice68.c
    file68/src/init68.c
    file68/src/msg68.c
    file68/src/option68.c
    file68/src/registry68.c
    file68/src/replay68.c
    file68/src/rsc68.c
    file68/src/string68.c
    file68/src/timedb68.c
    file68/src/uri68.c
    file68/src/vfs68.c
    file68/src/vfs68_ao.c
    file68/src/vfs68_curl.c
    file68/src/vfs68_fd.c
    file68/src/vfs68_file.c
    file68/src/vfs68_mem.c
    file68/src/vfs68_null.c
    file68/src/vfs68_z.c
)
target_include_directories(sc68_file68 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/sc68
)
target_include_directories(sc68_file68 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/file68
    ${CMAKE_CURRENT_SOURCE_DIR}/file68/src
    ${CMAKE_CURRENT_SOURCE_DIR}/unice68
)
target_compile_definitions(sc68_file68 PRIVATE HAVE_CONFIG_H)
target_compile_features(sc68_file68 PRIVATE c_std_99)
disable_sc68_sanitizers(sc68_file68)
target_link_libraries(sc68_file68 PUBLIC
    ZLIB::ZLIB
    sc68_unice68
)

add_library(sc68_lib STATIC
    libsc68/dial68/dial68.c
    libsc68/emu68/emu68.c
    libsc68/emu68/error68.c
    libsc68/emu68/getea68.c
    libsc68/emu68/inst68.c
    libsc68/emu68/ioplug68.c
    libsc68/emu68/line0_68.c
    libsc68/emu68/line1_68.c
    libsc68/emu68/line2_68.c
    libsc68/emu68/line3_68.c
    libsc68/emu68/line4_68.c
    libsc68/emu68/line5_68.c
    libsc68/emu68/line6_68.c
    libsc68/emu68/line7_68.c
    libsc68/emu68/line8_68.c
    libsc68/emu68/line9_68.c
    libsc68/emu68/lineA_68.c
    libsc68/emu68/lineB_68.c
    libsc68/emu68/lineC_68.c
    libsc68/emu68/lineD_68.c
    libsc68/emu68/lineE_68.c
    libsc68/emu68/lineF_68.c
    libsc68/emu68/mem68.c
    libsc68/emu68/table68.c
    libsc68/io68/io68.c
    libsc68/io68/mfp_io.c
    libsc68/io68/mfpemul.c
    libsc68/io68/mw_io.c
    libsc68/io68/mwemul.c
    libsc68/io68/paula_io.c
    libsc68/io68/paulaemul.c
    libsc68/io68/shifter_io.c
    libsc68/io68/ym_blep.c
    libsc68/io68/ym_dump.c
    libsc68/io68/ym_envel.c
    libsc68/io68/ym_io.c
    libsc68/io68/ym_puls.c
    libsc68/io68/ymemul.c
    libsc68/src/api68.c
    libsc68/src/conf68.c
    libsc68/src/libsc68.c
    libsc68/src/mixer68.c
)
target_include_directories(sc68_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/sc68
)
target_include_directories(sc68_lib PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/libsc68
    ${CMAKE_CURRENT_SOURCE_DIR}/libsc68/dial68
    ${CMAKE_CURRENT_SOURCE_DIR}/libsc68/emu68
    ${CMAKE_CURRENT_SOURCE_DIR}/libsc68/io68
    ${CMAKE_CURRENT_SOURCE_DIR}/libsc68/src
    ${CMAKE_CURRENT_SOURCE_DIR}/file68
    ${CMAKE_CURRENT_SOURCE_DIR}/file68/src
)
target_compile_definitions(sc68_lib PRIVATE
    HAVE_CONFIG_H
    YM_ENGINE=YM_ENGINE_BLEP
)
target_compile_features(sc68_lib PRIVATE c_std_99)
disable_sc68_sanitizers(sc68_lib)
target_link_libraries(sc68_lib PUBLIC sc68_file68)
