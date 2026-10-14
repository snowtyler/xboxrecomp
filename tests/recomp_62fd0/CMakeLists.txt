cmake_minimum_required(VERSION 3.20)
project(recomp_62fd0 C)
add_subdirectory(../.. runtime)
add_executable(recomp-62fd0-test abi_test.c
    ../../games/brute_force_e3/analysis/repair62fd0.c)
target_include_directories(recomp-62fd0-test PRIVATE
    ../../src/kernel ../../games/brute_force_e3/src/recomp/gen)
target_link_libraries(recomp-62fd0-test PRIVATE xboxrecomp)
target_compile_definitions(recomp-62fd0-test PRIVATE RECOMP_GENERATED_CODE)
target_compile_options(recomp-62fd0-test PRIVATE /UNDEBUG)
enable_testing()
add_test(NAME merged-return-stack COMMAND recomp-62fd0-test
    ${CMAKE_CURRENT_SOURCE_DIR}/../../tools/conformance/test.xbe)
add_executable(recomp-113570-test dispatch_test.c
    ../../games/brute_force_e3/analysis/repair113570.c)
target_include_directories(recomp-113570-test PRIVATE
    ../../src/kernel ../../games/brute_force_e3/src/recomp/gen)
target_link_libraries(recomp-113570-test PRIVATE xboxrecomp)
target_compile_definitions(recomp-113570-test PRIVATE RECOMP_GENERATED_CODE)
target_compile_options(recomp-113570-test PRIVATE /UNDEBUG)
add_test(NAME merged-message-dispatch-stack COMMAND recomp-113570-test
    ${CMAKE_CURRENT_SOURCE_DIR}/../../tools/conformance/test.xbe)
add_executable(recomp-418e0-test early_return_test.c
    ../../games/brute_force_e3/analysis/repair418e0.c)
target_include_directories(recomp-418e0-test PRIVATE
    ../../src/kernel ../../games/brute_force_e3/src/recomp/gen)
target_link_libraries(recomp-418e0-test PRIVATE xboxrecomp)
target_compile_definitions(recomp-418e0-test PRIVATE RECOMP_GENERATED_CODE)
target_compile_options(recomp-418e0-test PRIVATE /UNDEBUG)
add_test(NAME merged-flag-float-return-stack COMMAND recomp-418e0-test
    ${CMAKE_CURRENT_SOURCE_DIR}/../../tools/conformance/test.xbe)
add_executable(recomp-extra-boundaries-test extra_boundaries_test.c
    ../../games/brute_force_e3/analysis/repair00041D90.c
    ../../games/brute_force_e3/analysis/repair00100B60.c
    ../../games/brute_force_e3/analysis/repair00105810.c
    ../../games/brute_force_e3/analysis/repair001530D0.c
    ../../games/brute_force_e3/analysis/repair00186390.c)
target_include_directories(recomp-extra-boundaries-test PRIVATE
    ../../src/kernel ../../games/brute_force_e3/src/recomp/gen)
target_link_libraries(recomp-extra-boundaries-test PRIVATE xboxrecomp)
target_compile_definitions(recomp-extra-boundaries-test PRIVATE RECOMP_GENERATED_CODE)
target_compile_options(recomp-extra-boundaries-test PRIVATE /UNDEBUG)
add_test(NAME merged-extra-boundaries-stack COMMAND recomp-extra-boundaries-test
    ${CMAKE_CURRENT_SOURCE_DIR}/../../tools/conformance/test.xbe)
add_executable(recomp-class-getter-test class_getter_test.c
    ../../games/brute_force_e3/analysis/repair157d40.c)
target_include_directories(recomp-class-getter-test PRIVATE
    ../../src/kernel ../../games/brute_force_e3/src/recomp/gen)
target_link_libraries(recomp-class-getter-test PRIVATE xboxrecomp)
target_compile_definitions(recomp-class-getter-test PRIVATE RECOMP_GENERATED_CODE)
target_compile_options(recomp-class-getter-test PRIVATE /UNDEBUG)
add_test(NAME independent-class-getter COMMAND recomp-class-getter-test
    ${CMAKE_CURRENT_SOURCE_DIR}/../../tools/conformance/test.xbe)
