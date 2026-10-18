cmake_minimum_required(VERSION 3.20)
project(xaudio2_clock C)
set(CMAKE_C_STANDARD 11)
add_executable(xaudio2_clock probe.c ../../src/apu/apu_xaudio2.c)
target_link_libraries(xaudio2_clock PRIVATE xaudio2 ole32)
# Explicit hardware probe, deliberately excluded from automatic CTest runs.
