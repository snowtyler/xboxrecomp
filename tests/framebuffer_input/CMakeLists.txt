cmake_minimum_required(VERSION 3.20)
project(framebuffer_input C)
add_executable(framebuffer-input-test test_main.c
    ../../src/video/fb_present.c ../../src/input/xinput_device.c)
target_include_directories(framebuffer-input-test PRIVATE ../../src ../../src/input)
target_link_libraries(framebuffer-input-test PRIVATE user32 gdi32 xinput)
enable_testing()
add_test(NAME framebuffer_keyboard_input COMMAND framebuffer-input-test)
