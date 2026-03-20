add_executable(impulse
    main.cpp
    core/MediaSource.cpp
    core/Lyrics.cpp
    app/Application.cpp
    app/SettingsStorage.cpp
    app/SessionStorage.cpp
    audio/Decoder.cpp
    audio/DecoderProvider.cpp
    audio/FFmpegDecoderBackend.cpp
    audio/VgmDecoderBackend.cpp
    audio/AudioOutput.cpp
    metadata/MetadataReader.cpp
    metadata/FFmpegMetadataReader.cpp
    metadata/VgmMetadataReader.cpp
    playlist/PlaylistManager.cpp
    playlist/PlaylistWorkspace.cpp
    playlist/M3U8Playlist.cpp
    browser/FileBrowser.cpp
    core/SupportedFormats.cpp
    mpris/MprisModel.cpp
    mpris/MprisService.cpp
    ui/MainWindow.cpp
)

target_include_directories(impulse PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(impulse PRIVATE IMPULSE_APP_VERSION="${PROJECT_VERSION}")

target_link_libraries(impulse PRIVATE
    PkgConfig::LIBAV
    PkgConfig::PIPEWIRE
    PkgConfig::SDL3
    PkgConfig::SYSTEMD
    vgm-player
    vgm-emu
    vgm-utils
    imgui_sdl3
    readerwriterqueue
    pthread
)

install(TARGETS impulse RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES
    ${CMAKE_SOURCE_DIR}/packaging/linux/impulse.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES
    ${CMAKE_SOURCE_DIR}/packaging/linux/org.mpris.MediaPlayer2.impulse.service
    DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/services)

install(FILES
    ${CMAKE_SOURCE_DIR}/LICENSE
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/impulse)
install(FILES
    ${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.md
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/impulse)

set(IMPULSE_ICON_SIZES 16 24 32 48 64 128 256 512)
foreach(size IN LISTS IMPULSE_ICON_SIZES)
    install(FILES
        ${CMAKE_SOURCE_DIR}/packaging/linux/icons/hicolor/${size}x${size}/apps/impulse.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps)
endforeach()

target_compile_features(impulse PRIVATE cxx_std_23)
