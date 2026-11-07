add_library(pd_ipc STATIC src/cbor.cpp src/frame.cpp src/process.cpp src/value.cpp)
pd_warnings(pd_ipc)
target_include_directories(pd_ipc PUBLIC include)

add_executable(pd_ipc_test tests/frame_test.cpp)
target_link_libraries(pd_ipc_test PRIVATE pd_ipc)
pd_warnings(pd_ipc_test)
add_test(NAME ipc.frame COMMAND pd_ipc_test)

add_executable(pd_ipc_cbor_test tests/cbor_test.cpp)
target_link_libraries(pd_ipc_cbor_test PRIVATE pd_ipc)
pd_warnings(pd_ipc_cbor_test)
add_test(NAME ipc.cbor COMMAND pd_ipc_cbor_test ${PROJECT_SOURCE_DIR}/packages/protocol/vectors)

# The IPC benchmark (ADR-0004): needs the Pico echo backend
# (`pico build --release packages/communicator-pico/bench/echo.pi -o build/bench/echo`).
if(NOT WIN32)  # POSIX (it compares pipes with Unix-domain sockets)
  add_executable(pd_ipc_bench bench/ipc_bench.cpp)
  target_link_libraries(pd_ipc_bench PRIVATE pd_ipc)
  pd_warnings(pd_ipc_bench)
endif()

add_executable(pd_ipc_value_test tests/value_test.cpp)
target_link_libraries(pd_ipc_value_test PRIVATE pd_ipc)
pd_warnings(pd_ipc_value_test)
add_test(NAME ipc.value COMMAND pd_ipc_value_test)

# spawn_piped's failure paths close what they opened (#7); POSIX (it starves RLIMIT_NOFILE).
if(NOT WIN32)
  add_executable(pd_ipc_process_test tests/process_test.cpp)
  target_link_libraries(pd_ipc_process_test PRIVATE pd_ipc)
  pd_warnings(pd_ipc_process_test)
  add_test(NAME ipc.process COMMAND pd_ipc_process_test)
endif()

# Untrusted bytes through every reader (run it under the `sanitize` preset for memory errors).
add_executable(pd_ipc_fuzz_test tests/fuzz_test.cpp)
target_link_libraries(pd_ipc_fuzz_test PRIVATE pd_ipc)
pd_warnings(pd_ipc_fuzz_test)
add_test(NAME ipc.fuzz COMMAND pd_ipc_fuzz_test)

# A soak of the Pico side: 600 × 1 MiB echoes, the backend's peak memory bounded (needs pico and
# the built protocol package; skipped otherwise).
if(NOT WIN32)  # a bash script
  add_test(NAME ipc.soak COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/soak.sh)
  set_tests_properties(ipc.soak PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300)
endif()
