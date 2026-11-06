add_library(pd_ipc STATIC src/frame.cpp)
pd_warnings(pd_ipc)
target_include_directories(pd_ipc PUBLIC include)

add_executable(pd_ipc_test tests/frame_test.cpp)
target_link_libraries(pd_ipc_test PRIVATE pd_ipc)
pd_warnings(pd_ipc_test)
add_test(NAME ipc.frame COMMAND pd_ipc_test)
