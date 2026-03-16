include(FetchContent)

if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()

FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG        v2.4.12)

FetchContent_GetProperties(doctest)
if(NOT doctest_POPULATED)
    FetchContent_Populate(doctest)
endif()

function(add_impulse_test name sources)
    add_executable(${name} ${sources} ${CMAKE_SOURCE_DIR}/src/core/MediaSource.cpp)
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${doctest_SOURCE_DIR}/doctest)
    target_link_libraries(${name} PRIVATE readerwriterqueue)
    target_compile_features(${name} PRIVATE cxx_std_23)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_impulse_test(test_ringbuffer test_ringbuffer.cpp)
add_impulse_test(test_bitrate_span_queue test_bitrate_span_queue.cpp)
add_impulse_test(test_replay_gain test_replay_gain.cpp)
add_impulse_test(test_playback_status_utils test_playback_status_utils.cpp)
add_impulse_test(test_gapless_trimmer test_gapless_trimmer.cpp)
add_impulse_test(test_gapless_track_queue test_gapless_track_queue.cpp)
add_impulse_test(test_gapless_pending_request test_gapless_pending_request.cpp)

add_executable(test_playlist
    test_playlist.cpp
    ${CMAKE_SOURCE_DIR}/src/core/MediaSource.cpp
    ${CMAKE_SOURCE_DIR}/src/playlist/PlaylistManager.cpp
    ${CMAKE_SOURCE_DIR}/src/playlist/PlaylistWorkspace.cpp)
target_include_directories(test_playlist PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${doctest_SOURCE_DIR}/doctest)
target_link_libraries(test_playlist PRIVATE readerwriterqueue)
target_compile_features(test_playlist PRIVATE cxx_std_23)
add_test(NAME test_playlist COMMAND test_playlist)

add_executable(test_playlist_io
    test_playlist_io.cpp
    ${CMAKE_SOURCE_DIR}/src/core/MediaSource.cpp
    ${CMAKE_SOURCE_DIR}/src/playlist/M3U8Playlist.cpp)
target_include_directories(test_playlist_io PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${doctest_SOURCE_DIR}/doctest)
target_link_libraries(test_playlist_io PRIVATE readerwriterqueue PkgConfig::LIBAV)
target_compile_features(test_playlist_io PRIVATE cxx_std_23)
add_test(NAME test_playlist_io COMMAND test_playlist_io)

add_executable(test_file_browser
    test_file_browser.cpp
    ${CMAKE_SOURCE_DIR}/src/browser/FileBrowser.cpp)
target_include_directories(test_file_browser PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${doctest_SOURCE_DIR}/doctest)
target_link_libraries(test_file_browser PRIVATE readerwriterqueue)
target_compile_features(test_file_browser PRIVATE cxx_std_23)
add_test(NAME test_file_browser COMMAND test_file_browser)

# test_metadata needs MetadataReader compiled in
add_executable(test_metadata
    test_metadata.cpp
    ${CMAKE_SOURCE_DIR}/src/core/MediaSource.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Lyrics.cpp
    ${CMAKE_SOURCE_DIR}/src/metadata/MetadataReader.cpp)
target_include_directories(test_metadata PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${doctest_SOURCE_DIR}/doctest)
target_link_libraries(test_metadata PRIVATE PkgConfig::LIBAV)
target_compile_features(test_metadata PRIVATE cxx_std_23)
add_test(NAME test_metadata COMMAND test_metadata)

add_executable(test_lyrics
    test_lyrics.cpp
    ${CMAKE_SOURCE_DIR}/src/core/MediaSource.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Lyrics.cpp)
target_include_directories(test_lyrics PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${doctest_SOURCE_DIR}/doctest)
target_link_libraries(test_lyrics PRIVATE readerwriterqueue)
target_compile_features(test_lyrics PRIVATE cxx_std_23)
add_test(NAME test_lyrics COMMAND test_lyrics)

add_executable(test_decoder
    test_decoder.cpp
    ${CMAKE_SOURCE_DIR}/src/core/MediaSource.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Lyrics.cpp
    ${CMAKE_SOURCE_DIR}/src/audio/Decoder.cpp
    ${CMAKE_SOURCE_DIR}/src/metadata/MetadataReader.cpp)
target_include_directories(test_decoder PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${doctest_SOURCE_DIR}/doctest)
target_link_libraries(test_decoder PRIVATE PkgConfig::LIBAV)
target_compile_features(test_decoder PRIVATE cxx_std_23)
add_test(NAME test_decoder COMMAND test_decoder)

add_executable(test_session
    test_session.cpp
    ${CMAKE_SOURCE_DIR}/src/core/MediaSource.cpp
    ${CMAKE_SOURCE_DIR}/src/app/SessionStorage.cpp)
target_include_directories(test_session PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${doctest_SOURCE_DIR}/doctest)
target_link_libraries(test_session PRIVATE readerwriterqueue)
target_compile_features(test_session PRIVATE cxx_std_23)
add_test(NAME test_session COMMAND test_session)

add_executable(test_settings
    test_settings.cpp
    ${CMAKE_SOURCE_DIR}/src/app/SettingsStorage.cpp)
target_include_directories(test_settings PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${doctest_SOURCE_DIR}/doctest)
target_link_libraries(test_settings PRIVATE readerwriterqueue)
target_compile_features(test_settings PRIVATE cxx_std_23)
add_test(NAME test_settings COMMAND test_settings)

add_executable(test_mpris_model
    test_mpris_model.cpp
    ${CMAKE_SOURCE_DIR}/src/browser/FileBrowser.cpp
    ${CMAKE_SOURCE_DIR}/src/core/MediaSource.cpp
    ${CMAKE_SOURCE_DIR}/src/mpris/MprisModel.cpp)
target_include_directories(test_mpris_model PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${doctest_SOURCE_DIR}/doctest)
target_link_libraries(test_mpris_model PRIVATE readerwriterqueue)
target_compile_features(test_mpris_model PRIVATE cxx_std_23)
add_test(NAME test_mpris_model COMMAND test_mpris_model)
