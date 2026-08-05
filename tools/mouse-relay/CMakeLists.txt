cmake_minimum_required(VERSION 3.22)

project(BanditMouseRelay LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(ANDROID)
  find_package(SDL3 REQUIRED CONFIG)

  add_library(main SHARED src/main.cpp)
  target_compile_definitions(main PRIVATE BANDIT_MOUSE_RELAY_TOUCH=1)
  target_link_libraries(main PRIVATE SDL3::SDL3 android log)
elseif(IOS)
  include(FetchContent)

  # ios wont load an unembedded dylib from a sideloaded bundle
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)

  FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.10
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(SDL3)

  add_executable(BanditMouseRelay MACOSX_BUNDLE src/main.cpp)
  target_compile_definitions(BanditMouseRelay PRIVATE BANDIT_MOUSE_RELAY_TOUCH=1)
  target_link_libraries(BanditMouseRelay PRIVATE SDL3::SDL3-static)

  set_target_properties(BanditMouseRelay PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/ios/Info.plist"
    MACOSX_BUNDLE_BUNDLE_NAME "Bandit Relay"
    MACOSX_BUNDLE_GUI_IDENTIFIER "dev.bandit.mouserelay"
    MACOSX_BUNDLE_BUNDLE_VERSION "0.1.0"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1.0"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
    XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
    XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "NO"
    XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY ""
  )
else()
  include(FetchContent)

  set(SDL_SHARED ON CACHE BOOL "" FORCE)
  set(SDL_STATIC OFF CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)

  FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.10
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(SDL3)

  add_executable(BanditMouseRelay WIN32 src/main.cpp)
  target_link_libraries(BanditMouseRelay PRIVATE SDL3::SDL3)

  if(WIN32)
    target_sources(BanditMouseRelay PRIVATE src/app.rc)
    target_link_libraries(BanditMouseRelay PRIVATE ws2_32 winmm)
  endif()
endif()

