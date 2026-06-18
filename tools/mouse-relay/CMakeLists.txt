cmake_minimum_required(VERSION 3.22)

project(BanditMouseRelay LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(ANDROID)
  find_package(SDL3 REQUIRED CONFIG)

  add_library(main SHARED src/main.cpp)
  target_compile_definitions(main PRIVATE BANDIT_MOUSE_RELAY_ANDROID=1)
  target_link_libraries(main PRIVATE SDL3::SDL3 android log)
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

