add_library(crawler_core STATIC src/cli.cpp src/html_document.cpp)
target_include_directories(crawler_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(crawler_core PUBLIC cxx_std_20)
target_link_libraries(crawler_core PRIVATE lexbor_static)

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
    add_executable(html_document_test tests/html_document_test.cpp)
    target_link_libraries(html_document_test PRIVATE crawler_core GTest::gtest_main)
    target_compile_options(html_document_test PRIVATE -Wall -Wextra -Wpedantic)
    # Let CTest report and filter each GoogleTest case separately.
    gtest_discover_tests(html_document_test
        TEST_PREFIX "crawler."
        DISCOVERY_TIMEOUT 15
        PROPERTIES TIMEOUT 10
    )

    # Validate the C API from C++ before building extraction logic on top of it.
    add_executable(html_parser_test tests/html_parser_test.cpp)
    target_link_libraries(html_parser_test PRIVATE lexbor_static)
    target_compile_options(html_parser_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME crawler.html_parser COMMAND html_parser_test)
    set_tests_properties(crawler.html_parser PROPERTIES TIMEOUT 10)

    add_test(NAME crawler.cli
        COMMAND "${CMAKE_COMMAND}"
            "-DCRAWLER=$<TARGET_FILE:hfs-crawler>"
            "-DEXPECTED_VERSION=${PROJECT_VERSION}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_test.cmake"
    )
    set_tests_properties(crawler.cli PROPERTIES TIMEOUT 15)
endif()
