project(promise-cc-test VERSION 0.0.1)

add_executable(${PROJECT_NAME} test.cc)
target_link_libraries(${PROJECT_NAME} PRIVATE Catch2::Catch2WithMain)
target_link_libraries(${PROJECT_NAME} PRIVATE promise-cc)

catch_discover_tests(${PROJECT_NAME})