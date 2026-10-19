cmake_minimum_required(VERSION 3.22)
project(HzTestPlugin VERSION 1.0.0)
set(CMAKE_CXX_STANDARD 17)
add_subdirectory(${JUCE_DIR} juce EXCLUDE_FROM_ALL)
juce_add_plugin(HzTest
  COMPANY_NAME "Hangszonyeg teszt"
  PLUGIN_MANUFACTURER_CODE Hzts
  PLUGIN_CODE Hzt1
  FORMATS VST3
  PRODUCT_NAME "HZ Teszt")
target_sources(HzTest PRIVATE TestPlugin.cpp)
target_compile_definitions(HzTest PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(HzTest PRIVATE juce::juce_audio_utils PUBLIC juce::juce_recommended_config_flags)
