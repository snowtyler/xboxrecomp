cmake_minimum_required(VERSION 3.20)
project(nv2a_pb_control C)
add_executable(nv2a-pb-control-test control_test.c ../../src/kernel/nv2a_pb_scan.c
    ../../src/kernel/nv2a_order_trace.c)
target_compile_options(nv2a-pb-control-test PRIVATE /UNDEBUG)
enable_testing()
add_test(NAME nv2a-pushbuffer-control COMMAND nv2a-pb-control-test)
add_executable(nv2a-get-progress-test get_progress_test.c ../../src/kernel/nv2a_pb_scan.c
    ../../src/kernel/nv2a_order_trace.c)
target_compile_definitions(nv2a-get-progress-test PRIVATE NV2A_PB_TEST_HOOKS)
target_compile_options(nv2a-get-progress-test PRIVATE /UNDEBUG)
foreach(progress RANGE 0 1)
    foreach(coalesced RANGE 0 1)
        add_test(NAME nv2a-get-${progress}-coalesced-${coalesced}
            COMMAND nv2a-get-progress-test ${progress} ${coalesced})
    endforeach()
endforeach()
