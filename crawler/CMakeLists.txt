add_library(crawler_core STATIC src/cli.cpp)
target_include_directories(crawler_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(crawler_core PUBLIC cxx_std_20)

# Keep the version in project() as the single source of truth.
target_compile_definitions(crawler_core PRIVATE HFS_VERSION="${PROJECT_VERSION}")

add_executable(hfs-crawler src/main.cpp)
target_link_libraries(hfs-crawler PRIVATE crawler_core)

# Apply diagnostics to our code, without imposing them on future dependencies.
foreach(target IN ITEMS crawler_core hfs-crawler)
    target_compile_options(${target} PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow
    )
endforeach()

if(BUILD_TESTING)
    add_test(NAME crawler.cli
        COMMAND "${CMAKE_COMMAND}"
            "-DCRAWLER=$<TARGET_FILE:hfs-crawler>"
            "-DEXPECTED_VERSION=${PROJECT_VERSION}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_test.cmake"
    )
    set_tests_properties(crawler.cli PROPERTIES TIMEOUT 15)
endif()
