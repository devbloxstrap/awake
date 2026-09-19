cmake_minimum_required(VERSION 3.20)
project(Awake LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "Awake is a Windows-only application.")
endif()

add_executable(Awake WIN32 Awake.cpp)
target_compile_features(Awake PRIVATE cxx_std_17)
target_compile_definitions(Awake PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(Awake PRIVATE shell32 user32 kernel32)

if(MSVC)
  target_compile_options(Awake PRIVATE /W4 /permissive-)
endif()
