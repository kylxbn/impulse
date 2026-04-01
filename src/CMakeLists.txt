add_executable(impulse
    main.cpp
    core/MediaSource.cpp
    core/Lyrics.cpp
    core/Sc68Support.cpp
    app/Application.cpp
    app/SettingsStorage.cpp
    app/SessionStorage.cpp
    audio/Decoder.cpp
    audio/DecoderProvider.cpp
    audio/FFmpegDecoderBackend.cpp
    audio/OpenMptDecoderBackend.cpp
    audio/SapDecoderBackend.cpp
    audio/Sc68DecoderBackend.cpp
    audio/VgmDecoderBackend.cpp
    audio/AudioOutput.cpp
    metadata/MetadataReader.cpp
    metadata/FFmpegMetadataReader.cpp
    metadata/OpenMptMetadataReader.cpp
    metadata/SapMetadataReader.cpp
    metadata/Sc68MetadataReader.cpp
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

set(IMPULSE_APP_ID "com.kylxbn.impulse")
set(IMPULSE_APPSTREAM_METAINFO
    "${CMAKE_CURRENT_BINARY_DIR}/${IMPULSE_APP_ID}.metainfo.xml")

configure_file(
    "${CMAKE_SOURCE_DIR}/packaging/linux/${IMPULSE_APP_ID}.metainfo.xml.in"
    "${IMPULSE_APPSTREAM_METAINFO}"
    @ONLY)

target_include_directories(impulse PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(impulse PRIVATE
    IMPULSE_APP_VERSION="${PROJECT_VERSION}"
    IMPULSE_APP_SHARE_DIR="${CMAKE_INSTALL_FULL_DATADIR}/impulse"
    IMPULSE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

target_link_libraries(impulse PRIVATE
    PkgConfig::LIBAV
    PkgConfig::LIBOPENMPT
    PkgConfig::PIPEWIRE
    PkgConfig::SDL3
    PkgConfig::SYSTEMD
    vgm-player
    vgm-emu
    vgm-utils
    asap_lib
    sc68_lib
    imgui_sdl3
    readerwriterqueue
    pthread
)

install(TARGETS impulse RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES
    ${CMAKE_SOURCE_DIR}/packaging/linux/${IMPULSE_APP_ID}.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES
    ${IMPULSE_APPSTREAM_METAINFO}
    DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
install(FILES
    ${CMAKE_SOURCE_DIR}/packaging/linux/org.mpris.MediaPlayer2.impulse.service
    DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/services)

install(FILES
    ${CMAKE_SOURCE_DIR}/LICENSE
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/impulse)
install(FILES
    ${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.md
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/impulse)
install(FILES
    ${CMAKE_SOURCE_DIR}/packaging/fonts/NotoSansCJKjp-Regular.otf
    DESTINATION ${CMAKE_INSTALL_DATADIR}/impulse/fonts)
install(FILES
    ${CMAKE_SOURCE_DIR}/packaging/fonts/LICENSE.NotoSansCJK-OFL-1.1.txt
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/impulse)

set(IMPULSE_ICON_SIZES 16 24 32 48 64 128 256 512)
foreach(size IN LISTS IMPULSE_ICON_SIZES)
    install(FILES
        ${CMAKE_SOURCE_DIR}/packaging/linux/icons/hicolor/${size}x${size}/apps/impulse.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps
        RENAME ${IMPULSE_APP_ID}.png)
endforeach()

target_compile_features(impulse PRIVATE cxx_std_23)
