add_library(pd_ipc STATIC src/frame.cpp)
target_include_directories(pd_ipc PUBLIC include)

add_executable(pd_ipc_test tests/frame_test.cpp)
target_link_libraries(pd_ipc_test PRIVATE pd_ipc)
add_test(NAME ipc.frame COMMAND pd_ipc_test)
