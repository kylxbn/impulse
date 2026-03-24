add_library(asap_lib STATIC
    asap.c
    asap-stdio.c
)

target_include_directories(asap_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(asap_lib PRIVATE _GNU_SOURCE)
target_compile_features(asap_lib PRIVATE c_std_99)
target_link_libraries(asap_lib PUBLIC m)

if(COMMAND impulse_configure_third_party_target)
    impulse_configure_third_party_target(asap_lib)
else()
    target_compile_options(asap_lib PRIVATE -w
        $<$<CONFIG:Debug>:-fno-sanitize=address,undefined>)
endif()
