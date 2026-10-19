cmake_minimum_required(VERSION 3.22)
if(POLICY CMP0169)
  cmake_policy(SET CMP0169 OLD)   # FetchContent_Populate: a JUCE forrását beillesztés előtt kicsit módosítjuk
endif()

project(Hangszonyeg VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(HZ_HEADLESS "Ablak nélküli próbaváltozat (Linuxos tesztekhez)" OFF)
set(HZ_JUCE_TAG "8.0.10" CACHE STRING "A használt JUCE-változat")

include(FetchContent)
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG ${HZ_JUCE_TAG}
  GIT_SHALLOW TRUE)
FetchContent_GetProperties(JUCE)
if(NOT juce_POPULATED)
  FetchContent_Populate(JUCE)
  include(cmake/PatchJuce.cmake)
  add_subdirectory(${juce_SOURCE_DIR} ${juce_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

option(HZ_LINUX_UI "Linuxon is ablakos változat (csak fejlesztői próbához)" OFF)
set(HZ_UI "appwindow" CACHE STRING "A felület: appwindow (Edge/Chrome alkalmazásablak), webview2 (beépített ablak, csak Visual Studióval), none")
if(HZ_HEADLESS)
  set(HZ_UI "none")
endif()
if(HZ_UI STREQUAL "webview2" AND WIN32 AND MSVC)
  set(HZ_WEBVIEW TRUE)
  set(HZ_UI_MODE 1)
elseif(HZ_UI STREQUAL "none")
  set(HZ_WEBVIEW FALSE)
  set(HZ_UI_MODE 0)
else()
  set(HZ_WEBVIEW FALSE)
  set(HZ_UI_MODE 2)
endif()
message(STATUS "Hangszőnyeg felület: ${HZ_UI} (mód ${HZ_UI_MODE})")

juce_add_gui_app(Hangszonyeg
  PRODUCT_NAME "Hangszonyeg"
  COMPANY_NAME "Bordas Mate"
  VERSION ${PROJECT_VERSION}
  ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.png"
  NEEDS_WEBVIEW2 ${HZ_WEBVIEW}
  NEEDS_WEB_BROWSER ${HZ_LINUX_UI})

juce_generate_juce_header(Hangszonyeg)

target_sources(Hangszonyeg PRIVATE
  Source/Main.cpp
  Source/LocalServer.cpp
  Source/PluginHost.cpp)

juce_add_binary_data(HzData SOURCES
  web/index.html
  resources/icon.png)

target_compile_definitions(Hangszonyeg PRIVATE
  JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Hangszonyeg,JUCE_PRODUCT_NAME>"
  JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Hangszonyeg,JUCE_VERSION>"
  JUCE_PLUGINHOST_VST3=1
  JUCE_PLUGINHOST_AU=0
  JUCE_PLUGINHOST_LADSPA=0
  JUCE_PLUGINHOST_LV2=0
  JUCE_USE_CURL=0
  HZ_UI_MODE=${HZ_UI_MODE})

if(HZ_WEBVIEW)
  target_compile_definitions(Hangszonyeg PRIVATE JUCE_WEB_BROWSER=1 JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1)
elseif(HZ_LINUX_UI AND HZ_UI_MODE EQUAL 1)
  target_compile_definitions(Hangszonyeg PRIVATE JUCE_WEB_BROWSER=1)
else()
  target_compile_definitions(Hangszonyeg PRIVATE JUCE_WEB_BROWSER=0)
endif()

if(MSVC)
  target_compile_options(Hangszonyeg PRIVATE /utf-8)
endif()
if(MINGW)
  # egyetlen, önálló .exe: a MinGW futásidejű könyvtárai is belekerülnek
  target_link_options(Hangszonyeg PRIVATE -static -static-libgcc -static-libstdc++)
endif()

target_link_libraries(Hangszonyeg
  PRIVATE
    HzData
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_gui_extra
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
