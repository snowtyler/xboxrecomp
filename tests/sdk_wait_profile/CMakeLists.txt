cmake_minimum_required(VERSION 3.20)
project(sdk_wait_profile C)
set(CMAKE_C_STANDARD 11)
add_executable(sdk_wait_profile_test test_main.c)
target_include_directories(sdk_wait_profile_test PRIVATE ../../src ../../src/kernel)
target_compile_options(sdk_wait_profile_test PRIVATE
    $<$<C_COMPILER_ID:MSVC>:/UNDEBUG>
    $<$<NOT:$<C_COMPILER_ID:MSVC>>:-UNDEBUG>)
enable_testing()
add_test(NAME sdk_wait_profile COMMAND sdk_wait_profile_test)
add_test(NAME sdk_wait_profile_disabled COMMAND sdk_wait_profile_test disabled)
