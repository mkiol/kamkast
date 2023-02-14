# Copyright (C) 2022-2023 Michal Kosciesza <michal@mkiol.net>
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

cmake_minimum_required(VERSION 3.15)

option(with_v4l2 "enable v4l2 video sources" ON)
option(with_v4l2m2m "enable v4l2 video sources and v4l2m2m video encoder" ON)
option(with_nvenc "enable nvidia video encoder" ON)
option(with_x11_screen_capture "enable X11 screen capture video source" ON)
option(with_droidcam "enable gstreamer droidcam video source" OFF)
option(with_sfos "enable features specific for sfos" OFF)
option(with_sfos_screen_capture "enable sfos screen capture" OFF)

option(with_trace_logs "enable trace logging" OFF)
option(with_sanitizers "enable asan and ubsan in debug build" ON)
option(with_static_sanitizers "link asan and ubsan statically in debug build" OFF)

option(build_x264 "download x264, build and link statically" ON)
option(build_ffmpeg "download FFmpeg, build and link statically" ON)
option(build_mhd "downlaod Libmicrohttpd, build and link statically" ON)
option(build_fmt "download fmt, build and link statically" ON)
option(build_lame "download lame, build and link statically" ON)
option(build_ffnvc "download and include ffnvc" ON)
option(build_nasm "download nasm sources and build" ON)

if(with_nvenc)
    message(INFO "option with_nvenc is supported only on x86/x86_64 architectures")
endif()

if(with_sfos_screen_capture)
    set(with_sfos ON)
endif()

if(with_v4l2m2m)
    set(with_v4l2m2m ON)
endif()

if(with_sfos)
    set(with_v4l2 OFF)
    set(with_v4l2m2m OFF)
    set(with_x11_screen_capture OFF)
    set(with_nvenc OFF)
endif()

set(ffmpeg_source_url "https://ffmpeg.org/releases/ffmpeg-5.1.2.tar.gz")
set(ffmpeg_checksum "f44232183ae1ef814eac50dd382a2d7f")
set(ffnvc_source_url "https://github.com/FFmpeg/nv-codec-headers/releases/download/n11.1.5.2/nv-codec-headers-11.1.5.2.tar.gz")
set(ffnvc_checksum "94fd5479bf934595530d9bfae93c1c55")
set(mhd_source_url "https://ftpmirror.gnu.org/libmicrohttpd/libmicrohttpd-0.9.75.tar.gz")
set(mhd_checksum "aff64581937b53f3a23b05216ad2cd02")
set(fmt_source_url "https://github.com/fmtlib/fmt/releases/download/9.1.0/fmt-9.1.0.zip")
set(fmt_checksum "6133244fe8ef6f75c5601e8069b37b04")
set(x264_source_url "https://code.videolan.org/videolan/x264/-/archive/stable/x264-stable.tar.bz2")
set(x264_checksum "7bd5f01a556e643e6aaca48e3e391d50")
set(lame_source_url "https://altushost-swe.dl.sourceforge.net/project/lame/lame/3.100/lame-3.100.tar.gz")
set(lame_checksum "83e260acbe4389b54fe08e0bdbf7cddb")
set(nasm_source_url "https://www.nasm.us/pub/nasm/releasebuilds/2.15.05/nasm-2.15.05.tar.gz")
set(nams_checksum "4ab99e8e777c249f32d5c10e82c658f1")

set(info_id "kamkast")
set(info_binary_id ${info_id})
set(info_translations_id ${info_id})
if(with_sfos)
    set(info_binary_id "harbour-kamkast")
endif()
set(info_name "Kamkast")
set(info_org "org.mkiol")
set(info_desc "Kamkast - remote camera server")
set(info_author "Michal Kosciesza")
set(info_author_email "michal@mkiol.net")
set(info_copyright_year "2022-2023")
set(info_support_email "kamkast@mkiol.net")
set(info_webpage "https://github.com/mkiol/kamkast")
set(info_license "Mozilla Public License 2.0")
set(info_license_url "http://mozilla.org/MPL/2.0")
set(info_license_spdx "MPL-2.0")
set(info_translators_str "")
set(info_libs_str "Qt · GNU Libmicrohttpd · FFmpeg · LAME · x264 · {fmt} · mINI · cxxopts")

project(${info_id} VERSION 1.1.0)

# dirs

set(external_dir ${PROJECT_SOURCE_DIR}/external)
set(external_include_dir ${PROJECT_BINARY_DIR}/external/include)
set(external_lib_dir ${PROJECT_BINARY_DIR}/external/lib)
set(sfos_dir ${PROJECT_SOURCE_DIR}/sfos)
set(resources_dir ${PROJECT_SOURCE_DIR}/resources)
set(flatpak_dir ${PROJECT_SOURCE_DIR}/flatpak)
set(translations_dir ${PROJECT_SOURCE_DIR}/translations)
set(tools_dir ${PROJECT_SOURCE_DIR}/tools)
set(patches_dir ${PROJECT_SOURCE_DIR}/patches)

# sources

set(sources
    src/main.cpp
    src/options.cpp
    src/options.hpp
    src/databuffer.cpp
    src/databuffer.hpp
    src/kamkast.cpp
    src/kamkast.hpp
    src/event.cpp
    src/event.hpp
    src/noguieventloop.cpp
    src/noguieventloop.hpp
    src/settings.cpp
    src/settings.hpp
    src/logger.cpp
    src/logger.hpp
    src/avlogger.cpp
    src/avlogger.hpp
    src/caster.cpp
    src/caster.hpp
    src/httpserver.cpp
    src/httpserver.hpp
    src/fftools.cpp
    src/fftools.hpp
    src/testsource.cpp
    src/testsource.hpp)

if(with_sfos)
    set(CMAKE_AUTOMOC ON)
    list(APPEND sources
        src/qtlogger.cpp
        src/qtlogger.hpp
        src/sfosgui.cpp
        src/sfosgui.hpp)
endif()

if(with_sfos_screen_capture)
    list(APPEND sources
        src/lipstickrecordersource.cpp
        src/lipstickrecordersource.hpp
        src/wayland-lipstick-recorder-protocol.c
        src/wayland-lipstick-recorder-client-protocol.h
        src/lipstick-recorder.h)
endif()

if(with_droidcam)
    list(APPEND sources
        src/droidcamsource.cpp
        src/droidcamsource.hpp
        src/orientationmonitor.cpp
        src/orientationmonitor.hpp)
endif()

# executable

add_executable(${info_binary_id} ${sources})

# strip

add_custom_command(TARGET ${info_binary_id} POST_BUILD
    COMMAND $<$<CONFIG:Release>:${CMAKE_STRIP}>
    ARGS --strip-all $<TARGET_FILE:${info_binary_id}>
)

# flags and definitions

add_library(compiler_flags INTERFACE)
target_compile_features(compiler_flags INTERFACE cxx_std_17)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    if(with_sanitizers)
        target_compile_options(compiler_flags INTERFACE
            "-pie;-Wall;-Wextra;-Wpedantic;-O1;-g3;-fsanitize=address,undefined")
        if(with_static_sanitizers)
            target_link_options(compiler_flags INTERFACE
              "-pie;-static-libasan;-static-libubsan;-fsanitize=address,undefined")
        else()
            target_link_options(compiler_flags INTERFACE "-pie;-fsanitize=address,undefined")
        endif()
    else()
        target_compile_options(compiler_flags INTERFACE
            "-pie;-Wall;-Wextra;-Wpedantic;-O1;-g3")
        target_link_options(compiler_flags INTERFACE "-pie")
    endif()
else()
    target_compile_options(compiler_flags INTERFACE "-pie;-Wall;-Wextra;-Wpedantic;-O3;-g")
    target_link_options(compiler_flags INTERFACE "-pie")
endif()

target_compile_definitions(compiler_flags INTERFACE "$<$<BOOL:${with_trace_logs}>:USE_TRACE_LOGS>")
target_compile_definitions(compiler_flags INTERFACE "$<$<BOOL:${with_droidcam}>:USE_DROIDCAM>")
target_compile_definitions(compiler_flags INTERFACE "$<$<BOOL:${with_v4l2}>:USE_V4L2>")
target_compile_definitions(compiler_flags INTERFACE "$<$<BOOL:${with_v4l2m2m}>:USE_V4L2>")
target_compile_definitions(compiler_flags INTERFACE "$<$<BOOL:${with_x11_screen_capture}>:USE_X11CAPTURE>")
target_compile_definitions(compiler_flags INTERFACE "$<$<BOOL:${with_sfos}>:USE_SFOS>")
target_compile_definitions(compiler_flags INTERFACE "$<$<BOOL:${with_sfos_screen_capture}>:USE_LIPSTICK_RECORDER>")
target_compile_definitions(compiler_flags INTERFACE "$<$<CONFIG:Debug>:USE_TESTSOURCE>")
target_compile_definitions(compiler_flags INTERFACE "$<$<CONFIG:Debug>:DEBUG>")

set_property(TARGET ${info_binary_id} PROPERTY POSITION_INDEPENDENT_CODE ON)

target_link_libraries(${info_binary_id}
    compiler_flags
    pthread
    ${CMAKE_DL_LIBS}
)

# config

configure_file(config.h.in config.h)
target_include_directories(${info_binary_id} PUBLIC ${PROJECT_BINARY_DIR})
target_include_directories(${info_binary_id} PRIVATE ${external_include_dir})

# translations

if(with_sfos)
    set(enabled_translations "en pl")

    find_package(Qt5 COMPONENTS Core LinguistTools)

    file(GLOB ts_files ${translations_dir}/*.ts)
    qt5_create_translation(qm_files ${CMAKE_SOURCE_DIR}/src ${sfos_dir}/qml ${ts_files})

    add_custom_command(
      OUTPUT translations.qrc
      COMMAND sh -c "${tools_dir}/make_translations_qrc.sh ${info_translations_id} /translations ${enabled_translations}" >
        "${CMAKE_BINARY_DIR}/translations.qrc"
      DEPENDS ${qm_files}
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
      VERBATIM)
    add_library(translations STATIC "${CMAKE_BINARY_DIR}/translations.qrc")
    target_link_libraries(${info_binary_id} translations)
endif()

# config

configure_file(config.h.in config.h)
target_include_directories(${info_binary_id} PRIVATE ${PROJECT_BINARY_DIR})
target_include_directories(${info_binary_id} PRIVATE ${external_include_dir})

# dependencies

include(FindPkgConfig)
include(ExternalProject)

add_custom_command(
  OUTPUT webui.h
  COMMAND sh -c "${tools_dir}/bin2header.sh ${resources_dir}/webui.html webui" >
    "${CMAKE_BINARY_DIR}/webui.h"
  DEPENDS ${resources_dir}/webui.html
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  VERBATIM)
add_library(webui STATIC "${CMAKE_BINARY_DIR}/webui.h")
set_target_properties(webui PROPERTIES LINKER_LANGUAGE C)
add_dependencies(${info_binary_id} webui)

pkg_search_module(pulse REQUIRED libpulse)
target_include_directories(${info_binary_id} PRIVATE ${pulse_INCLUDE_DIRS})
target_link_libraries(${info_binary_id} ${pulse_LIBRARIES})

if(with_droidcam)
    pkg_search_module(gst REQUIRED gstreamer-1.0)
    target_include_directories(${info_binary_id} PRIVATE ${gst_INCLUDE_DIRS})
    target_link_libraries(${info_binary_id} ${gst_LIBRARIES})

    pkg_search_module(gst_app REQUIRED gstreamer-app-1.0)
    target_include_directories(${info_binary_id} PRIVATE ${gst_app_INCLUDE_DIRS})
    target_link_libraries(${info_binary_id} ${gst_app_LIBRARIES})
endif()

if(with_sfos)
    find_package(Qt5 COMPONENTS Core Network Qml Gui Quick REQUIRED)
    target_link_libraries(${info_binary_id} Qt5::Core Qt5::Network Qt5::Gui Qt5::Quick)

    pkg_search_module(sailfish sailfishapp REQUIRED)
    target_include_directories(${info_binary_id} PRIVATE ${sailfish_INCLUDE_DIRS})
    target_link_libraries(${info_binary_id} ${sailfish_LIBRARIES})

    target_compile_definitions(${info_binary_id} PRIVATE $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:QT_QML_DEBUG>)
endif()

if (with_sfos_screen_capture)
    pkg_search_module(wayland wayland-client REQUIRED)
    target_include_directories(${info_binary_id} PRIVATE ${wayland_INCLUDE_DIRS})
    target_link_libraries(${info_binary_id} ${wayland_LIBRARIES})
endif()

if(with_x11_screen_capture)
    pkg_search_module(xcb REQUIRED xcb)
    include_directories(${xcb_INCLUDE_DIRS})
    target_link_libraries(${info_binary_id} ${xcb_LIBRARIES})

    pkg_search_module(x11 REQUIRED x11)
    include_directories(${x11_INCLUDE_DIRS})
    target_link_libraries(${info_binary_id} ${x11_LIBRARIES})
endif()

if(build_ffmpeg)
    set(ffmpeg_opts
        --disable-autodetect
        --disable-doc
        --disable-programs
        --disable-everything
        --enable-static
        --disable-shared
        --enable-nonfree
        --enable-gpl
        --enable-pic
        --enable-protocol=file
        --enable-filter=vflip
        --enable-filter=hflip
        --enable-filter=scale
        --enable-filter=transpose
        --enable-filter=color
        --enable-filter=overlay
        --enable-filter=pad
        --enable-filter=volume
        --enable-filter=amix
        --enable-filter=dynaudnorm
        --enable-filter=aresample
        --enable-filter=aformat
        --enable-encoder=libx264
        --enable-encoder=aac
        --enable-encoder=libmp3lame
        --enable-decoder=h264
        --enable-decoder=rawvideo
        --enable-decoder=pcm_u8
        --enable-decoder=pcm_u32le
        --enable-decoder=pcm_u32be
        --enable-decoder=pcm_u24le
        --enable-decoder=pcm_u24be
        --enable-decoder=pcm_u16le
        --enable-decoder=pcm_u16be
        --enable-decoder=pcm_s8
        --enable-decoder=pcm_s32le
        --enable-decoder=pcm_s32be
        --enable-decoder=pcm_s24le
        --enable-decoder=pcm_s24be
        --enable-decoder=pcm_s16le
        --enable-decoder=pcm_s16be
        --enable-decoder=pcm_f64le
        --enable-decoder=pcm_f64be
        --enable-decoder=pcm_f32le
        --enable-decoder=pcm_f32be
        --enable-decoder=aac
        --enable-decoder=aac_fixed
        --enable-decoder=aac_latm
        --enable-decoder=mp3
        --enable-decoder=mp3adu
        --enable-decoder=mp3adufloat
        --enable-decoder=mp3float
        --enable-decoder=mp3on4
        --enable-decoder=mp3on4float
        --enable-muxer=mp4
        --enable-muxer=mpegts
        --enable-muxer=mp3
        --enable-demuxer=mpegts
        --enable-demuxer=h264
        --enable-demuxer=rawvideo
        --enable-demuxer=aac
        --enable-demuxer=mp3
        --enable-demuxer=mov
        --enable-demuxer=ogg
        --enable-demuxer=matroska
        --enable-demuxer=flac
        --enable-demuxer=wav
        --enable-demuxer=mpegvideo
        --enable-parser=h264
        --enable-parser=aac
        --enable-parser=aac_latm
        --enable-parser=ac3
        --enable-bsf=h264_mp4toannexb
        --enable-bsf=dump_extradata
        --enable-bsf=extract_extradata
        --enable-libx264
        --enable-libmp3lame)

    if(${with_v4l2} OR ${with_v4l2m2m})
        list(APPEND ffmpeg_opts
            --enable-indev=v4l2)
    endif()

    if(with_v4l2m2m)
        list(APPEND ffmpeg_opts
            --enable-encoder=h264_v4l2m2m
            --enable-v4l2_m2m)
    endif()

    if(with_nvenc)
        list(APPEND ffmpeg_opts
            --enable-encoder=h264_nvenc
            --enable-nvenc
            --enable-ffnvcodec)
    endif()

    if(with_x11_screen_capture)
        list(APPEND ffmpeg_opts
            --enable-indev=xcbgrab
            --enable-libxcb)
    endif()

    ExternalProject_Add(ffmpeg
        SOURCE_DIR ${external_dir}/ffmpeg
        BINARY_DIR ${PROJECT_BINARY_DIR}/external/ffmpeg
        INSTALL_DIR ${PROJECT_BINARY_DIR}/external
        URL "${ffmpeg_source_url}"
        PATCH_COMMAND patch --forward --unified ${external_dir}/ffmpeg/configure
            -i ${patches_dir}/ffmpeg_v4l2.patch ||
                echo "patch cmd failed, likely already patched"
        CONFIGURE_COMMAND CPATH=${external_include_dir}
            LIBRARY_PATH=${external_lib_dir}
            PATH=$ENV{PATH}:${PROJECT_BINARY_DIR}/external/bin PKG_CONFIG_PATH=${external_lib_dir}/pkgconfig
            <SOURCE_DIR>/configure --prefix=<INSTALL_DIR> ${ffmpeg_opts} --extra-ldflags=-L${external_lib_dir}
        BUILD_COMMAND CPATH=${external_include_dir}
            LIBRARY_PATH=${external_lib_dir}
            PATH=$ENV{PATH}:${PROJECT_BINARY_DIR}/external/bin ${MAKE}
        BUILD_ALWAYS False
        INSTALL_COMMAND CPATH=${external_include_dir}
            LIBRARY_PATH=${external_lib_dir}
            PATH=$ENV{PATH}:${PROJECT_BINARY_DIR}/external/bin make DESTDIR=/ install)
    find_library(libm_path m)
    target_include_directories(${info_binary_id} PRIVATE ${external_include_dir})
    set(ffmpeg_LIBRARIES
        ${PROJECT_BINARY_DIR}/external/lib/libavfilter.a
        ${PROJECT_BINARY_DIR}/external/lib/libavdevice.a
        ${PROJECT_BINARY_DIR}/external/lib/libavformat.a
        ${PROJECT_BINARY_DIR}/external/lib/libavcodec.a
        ${PROJECT_BINARY_DIR}/external/lib/libswresample.a
        ${PROJECT_BINARY_DIR}/external/lib/libswscale.a
        ${PROJECT_BINARY_DIR}/external/lib/libavutil.a
        ${libm_path})
    target_link_libraries(${info_binary_id} ${ffmpeg_LIBRARIES})
    add_dependencies(${info_binary_id} ffmpeg)
else()
    pkg_search_module(ffmpeg_avutil REQUIRED libavutil>=57.17.100)
    pkg_search_module(ffmpeg_avcodec REQUIRED libavcodec>=59.18.100)
    pkg_search_module(ffmpeg_avformat REQUIRED libavformat>=59.16.100)
    pkg_search_module(ffmpeg_avdevice REQUIRED libavdevice>=59.7.100)
    pkg_search_module(ffmpeg_avfilter REQUIRED libavfilter>=8.44.100)
    pkg_search_module(ffmpeg_swresample REQUIRED libswresample>=4.3.100)
    pkg_search_module(ffmpeg_swscale REQUIRED libswscale>=6.7.100)
    target_include_directories(${info_binary_id} PRIVATE
        ${ffmpeg_avutil_INCLUDE_DIRS}
        ${ffmpeg_avcoodec_INCLUDE_DIRS}
        ${ffmpeg_avformat_INCLUDE_DIRS}
        ${ffmpeg_avdevice_INCLUDE_DIRS}
        ${ffmpeg_avfilter_INCLUDE_DIRS}
        ${ffmpeg_swresample_INCLUDE_DIRS}
        ${ffmpeg_swscale_INCLUDE_DIRS})
    target_link_libraries(${info_binary_id}
        ${ffmpeg_avdevice_LIBRARIES}
        ${ffmpeg_avfilter_LIBRARIES}
        ${ffmpeg_avformat_LIBRARIES}
        ${ffmpeg_avcoodec_LIBRARIES}
        ${ffmpeg_swresample_LIBRARIES}
        ${ffmpeg_swscale_LIBRARIES}
        ${ffmpeg_avutil_LIBRARIES})
endif()

if(build_x264)
    ExternalProject_Add(x264
        SOURCE_DIR ${external_dir}/x264
        BINARY_DIR ${PROJECT_BINARY_DIR}/external/x264
        INSTALL_DIR ${PROJECT_BINARY_DIR}/external
        URL "${x264_source_url}"
        URL_MD5 "${x264_checksum}"
        CONFIGURE_COMMAND PATH=$ENV{PATH}:${PROJECT_BINARY_DIR}/external/bin <SOURCE_DIR>/configure --prefix=<INSTALL_DIR> --enable-pic --enable-static --disable-cli
        BUILD_COMMAND PATH=$ENV{PATH}:${PROJECT_BINARY_DIR}/external/bin ${MAKE}
        BUILD_ALWAYS False
        INSTALL_COMMAND PATH=$ENV{PATH}:${PROJECT_BINARY_DIR}/external/bin make DESTDIR=/ install)
    ExternalProject_Add_StepDependencies(ffmpeg configure x264)
    set(x264_LIBRARIES ${external_lib_dir}/libx264.a)
    target_include_directories(${info_binary_id} PRIVATE ${external_include_dir})
    target_link_libraries(${info_binary_id} ${x264_LIBRARIES} ${CMAKE_DL_LIBS})
    add_dependencies(${info_binary_id} x264)
else()
    pkg_search_module(x264 REQUIRED x264)
    target_include_directories(${info_binary_id} PRIVATE ${x264_INCLUDE_DIRS})
    target_link_libraries(${info_binary_id} ${x264_LIBRARIES})
endif()

if(with_nvenc)
    if (build_ffnvc)
        ExternalProject_Add(ffnvc
            SOURCE_DIR ${external_dir}/ffnvc
            BINARY_DIR ${PROJECT_BINARY_DIR}/external/ffnvc
            INSTALL_DIR ${PROJECT_BINARY_DIR}/external
            URL "${ffnvc_source_url}"
            URL_MD5 "${ffnvc_checksum}"
            CONFIGURE_COMMAND cp -r --no-target-directory <SOURCE_DIR> <BINARY_DIR>
            BUILD_COMMAND ""
            BUILD_ALWAYS False
            INSTALL_COMMAND make DESTDIR=/ PREFIX=<INSTALL_DIR> install)
        ExternalProject_Add_StepDependencies(ffmpeg configure ffnvc)
        target_include_directories(${info_binary_id} PRIVATE ${external_include_dir})
    endif()
endif()

if(build_lame)
    ExternalProject_Add(lame
        SOURCE_DIR ${external_dir}/lame
        BINARY_DIR ${PROJECT_BINARY_DIR}/external/lame
        INSTALL_DIR ${PROJECT_BINARY_DIR}/external
        URL "${lame_source_url}"
        URL_MD5 "${lame_checksum}"
        CONFIGURE_COMMAND <SOURCE_DIR>/configure --prefix=<INSTALL_DIR> --bindir=<INSTALL_DIR>/bin
            --enable-static --enable-nasm --disable-decoder --disable-analyzer-hooks
            --disable-frontend --with-pic=yes
        BUILD_COMMAND ${MAKE}
        BUILD_ALWAYS False
        INSTALL_COMMAND make DESTDIR=/ install)
    ExternalProject_Add_StepDependencies(ffmpeg configure lame)
    set(lame_LIBRARIES ${external_lib_dir}/libmp3lame.a)
    target_include_directories(${info_binary_id} PRIVATE ${external_include_dir})
    target_link_libraries(${info_binary_id} ${lame_LIBRARIES})
    add_dependencies(${info_binary_id} lame)
else()
    find_library(lame_path mp3lame)
    target_link_libraries(${info_binary_id} ${lame_path})
endif()

if(build_mhd)
    ExternalProject_Add(mhd
        SOURCE_DIR ${external_dir}/mhd
        BINARY_DIR ${PROJECT_BINARY_DIR}/external/mhd
        INSTALL_DIR ${PROJECT_BINARY_DIR}/external
        URL "${mhd_source_url}"
        URL_MD5 "${mhd_checksum}"
        CONFIGURE_COMMAND <SOURCE_DIR>/configure --prefix=<INSTALL_DIR>
        --disable-doc --disable-examples --disable-curl --disable-https
        --disable-postprocessor --disable-dauth --disable-bauth --disable-epoll
        --disable-sendfile --disable-httpupgrade
        --enable-static --disable-shared --with-pic=yes
        BUILD_COMMAND ${MAKE}
        BUILD_ALWAYS False
        INSTALL_COMMAND make DESTDIR=/ install)
    set(mhd_LIBRARIES ${external_lib_dir}/libmicrohttpd.a)
    target_include_directories(${info_binary_id} PRIVATE ${external_include_dir})
    target_link_libraries(${info_binary_id} ${mhd_LIBRARIES})
    add_dependencies(${info_binary_id} mhd)
else()
    pkg_search_module(mhd REQUIRED libmicrohttpd)
    target_include_directories(${info_binary_id} PRIVATE ${mhd_INCLUDE_DIRS})
    target_link_libraries(${info_binary_id} ${mhd_LIBRARIES})
endif()

if(build_fmt)
    ExternalProject_Add(fmt
        SOURCE_DIR ${external_dir}/fmt
        BINARY_DIR ${PROJECT_BINARY_DIR}/external/fmt
        INSTALL_DIR ${PROJECT_BINARY_DIR}/external
        URL "${fmt_source_url}"
        URL_MD5 "${fmt_checksum}"
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=/ -DCMAKE_INSTALL_LIBDIR=/lib
            -DCMAKE_INSTALL_INCLUDEDIR=/include -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DFMT_DOC=OFF
            -DFMT_TEST=OFF -DFMT_FUZZ=OFF
        BUILD_ALWAYS False
        INSTALL_COMMAND make DESTDIR=${PROJECT_BINARY_DIR}/external install/local)
    set(fmt_LIBRARIES ${external_lib_dir}/libfmt.a)
    target_include_directories(${info_binary_id} PRIVATE ${external_include_dir})
    target_link_libraries(${info_binary_id} ${fmt_LIBRARIES})
    add_dependencies(${info_binary_id} fmt)
else()
    pkg_search_module(fmt REQUIRED fmt)
    target_include_directories(${info_binary_id} PRIVATE ${fmt_INCLUDE_DIRS})
    target_link_libraries(${info_binary_id} ${fmt_LIBRARIES})
endif()

if(build_nasm)
    ExternalProject_Add(nasm
        SOURCE_DIR ${external_dir}/nasm
        BINARY_DIR ${PROJECT_BINARY_DIR}/external/nasm
        INSTALL_DIR ${PROJECT_BINARY_DIR}/external
        URL "${nasm_source_url}"
        URL_MD5 "${nasm_checksum}"
        CONFIGURE_COMMAND <SOURCE_DIR>/configure --prefix=<INSTALL_DIR>
        BUILD_COMMAND ${MAKE}
        BUILD_ALWAYS False
        INSTALL_COMMAND make DESTDIR=/ install)
    ExternalProject_Add_StepDependencies(ffmpeg configure nasm)
    ExternalProject_Add_StepDependencies(x264 configure nasm)
    if(build_lame)
        ExternalProject_Add_StepDependencies(lame configure nasm)
    endif()
endif()

# install

install(TARGETS ${info_binary_id} RUNTIME DESTINATION bin)

if(with_sfos)
    install(FILES ${sfos_dir}/${info_binary_id}.desktop DESTINATION share/applications)

    install(FILES ${sfos_dir}/icons/86x86/${info_binary_id}.png DESTINATION share/icons/hicolor/86x86/apps)
    install(FILES ${sfos_dir}/icons/108x108/${info_binary_id}.png DESTINATION share/icons/hicolor/108x108/apps)
    install(FILES ${sfos_dir}/icons/128x128/${info_binary_id}.png DESTINATION share/icons/hicolor/128x128/apps)
    install(FILES ${sfos_dir}/icons/172x172/${info_binary_id}.png DESTINATION share/icons/hicolor/172x172/apps)

    install(DIRECTORY ${sfos_dir}/qml DESTINATION share/${info_binary_id})
endif()

# misc

if(with_sfos)
    file(WRITE "${CMAKE_BINARY_DIR}/QtCreatorDeployment.txt"
        "${CMAKE_INSTALL_PREFIX}\n${CMAKE_BINARY_DIR}/${info_binary_id}:bin\n")
endif()

