# Engine regression tests. Links zues_core directly (the World API is
# ZUES_API-exported) and runs as a single dependency-free executable.
# Registered with CTest so `ctest` / `cmake --build --target test` runs it.

add_executable(zues_tests test_ecs.cpp)
target_link_libraries(zues_tests PRIVATE zues_core)

add_test(NAME ecs_regression COMMAND zues_tests)
