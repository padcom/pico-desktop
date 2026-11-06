add_executable(pico-desktop-host src/main.cpp)
target_link_libraries(pico-desktop-host PRIVATE pd_ipc)
target_compile_definitions(pico-desktop-host PRIVATE PD_VERSION="${PROJECT_VERSION}")

if(PD_WITH_CEF)
  message(FATAL_ERROR "PD_WITH_CEF: the CEF integration is phase F1 and isn't implemented yet")
endif()

add_test(NAME host.version COMMAND pico-desktop-host --version)
set_tests_properties(host.version PROPERTIES PASS_REGULAR_EXPRESSION "^pico-desktop-host ${PROJECT_VERSION}")
add_test(NAME host.unknown-option COMMAND pico-desktop-host --bogus)
set_tests_properties(host.unknown-option PROPERTIES WILL_FAIL TRUE)
