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
add_executable(pd_ipc_bench bench/ipc_bench.cpp)
target_link_libraries(pd_ipc_bench PRIVATE pd_ipc)
pd_warnings(pd_ipc_bench)

add_executable(pd_ipc_value_test tests/value_test.cpp)
target_link_libraries(pd_ipc_value_test PRIVATE pd_ipc)
pd_warnings(pd_ipc_value_test)
add_test(NAME ipc.value COMMAND pd_ipc_value_test)
