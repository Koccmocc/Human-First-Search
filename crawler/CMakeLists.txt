add_library(crawler_core STATIC
    src/cli.cpp src/html_document.cpp src/url.cpp src/http_client.cpp src/http_fetch.cpp
    src/robots.cpp src/robots_loading.cpp src/robots_cache.cpp src/page_fetcher.cpp
    src/host_delay.cpp src/url_frontier.cpp src/crawl.cpp src/html_input.cpp src/page_identity.cpp
    src/jsonl.cpp src/crawl_log.cpp)
target_include_directories(crawler_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(crawler_core PUBLIC cxx_std_20)
target_link_libraries(crawler_core PRIVATE lexbor_static CURL::libcurl_static hfs_robots_parser)
target_link_libraries(crawler_core PRIVATE nlohmann_json::nlohmann_json)
if(WIN32)
    # SHA-256 uses the operating system provider, without an extra runtime DLL.
    target_link_libraries(crawler_core PRIVATE bcrypt)
else()
    # The existing non-Windows curl configuration also requires OpenSSL.
    find_package(OpenSSL REQUIRED COMPONENTS Crypto)
    target_link_libraries(crawler_core PRIVATE OpenSSL::Crypto)
endif()

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
    add_executable(jsonl_test tests/jsonl_test.cpp tests/crawl_log_test.cpp)
    target_link_libraries(jsonl_test PRIVATE crawler_core GTest::gtest_main nlohmann_json::nlohmann_json)
    target_compile_options(jsonl_test PRIVATE -Wall -Wextra -Wpedantic)
    gtest_discover_tests(jsonl_test TEST_PREFIX "crawler." DISCOVERY_TIMEOUT 15 PROPERTIES TIMEOUT 10)

    add_executable(page_identity_test tests/page_identity_test.cpp)
    target_link_libraries(page_identity_test PRIVATE crawler_core GTest::gtest_main)
    target_compile_options(page_identity_test PRIVATE -Wall -Wextra -Wpedantic)
    gtest_discover_tests(page_identity_test TEST_PREFIX "crawler." DISCOVERY_TIMEOUT 15 PROPERTIES TIMEOUT 10)

    add_executable(cli_arguments_test tests/cli_arguments_test.cpp)
    target_link_libraries(cli_arguments_test PRIVATE crawler_core GTest::gtest_main)
    target_compile_options(cli_arguments_test PRIVATE -Wall -Wextra -Wpedantic)
    gtest_discover_tests(cli_arguments_test TEST_PREFIX "crawler." DISCOVERY_TIMEOUT 15 PROPERTIES TIMEOUT 10)

    add_executable(html_input_test tests/html_input_test.cpp)
    target_link_libraries(html_input_test PRIVATE crawler_core GTest::gtest_main)
    target_compile_options(html_input_test PRIVATE -Wall -Wextra -Wpedantic)
    gtest_discover_tests(html_input_test TEST_PREFIX "crawler." DISCOVERY_TIMEOUT 15 PROPERTIES TIMEOUT 10)

    add_executable(url_frontier_test tests/url_frontier_test.cpp)
    target_link_libraries(url_frontier_test PRIVATE crawler_core GTest::gtest_main)
    target_compile_options(url_frontier_test PRIVATE -Wall -Wextra -Wpedantic)
    gtest_discover_tests(url_frontier_test TEST_PREFIX "crawler." DISCOVERY_TIMEOUT 15 PROPERTIES TIMEOUT 10)

    add_executable(host_delay_test tests/host_delay_test.cpp)
    target_link_libraries(host_delay_test PRIVATE crawler_core GTest::gtest_main)
    target_compile_options(host_delay_test PRIVATE -Wall -Wextra -Wpedantic)
    gtest_discover_tests(host_delay_test TEST_PREFIX "crawler." DISCOVERY_TIMEOUT 15 PROPERTIES TIMEOUT 10)

    add_executable(robots_test tests/robots_test.cpp)
    target_link_libraries(robots_test PRIVATE crawler_core GTest::gtest_main)
    target_compile_options(robots_test PRIVATE -Wall -Wextra -Wpedantic)
    gtest_discover_tests(robots_test TEST_PREFIX "crawler." DISCOVERY_TIMEOUT 15 PROPERTIES TIMEOUT 10)

    add_executable(robots_parser_test tests/robots_parser_test.cpp)
    target_link_libraries(robots_parser_test PRIVATE hfs_robots_parser GTest::gtest_main)
    target_compile_options(robots_parser_test PRIVATE -Wall -Wextra -Wpedantic)
    gtest_discover_tests(robots_parser_test TEST_PREFIX "crawler." DISCOVERY_TIMEOUT 15 PROPERTIES TIMEOUT 10)

    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME crawler.cli_crawl COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/http_fixture.py" "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_crawl_test.py" "$<TARGET_FILE:hfs-crawler>")
    # This suite launches the CLI repeatedly; leave room for slower Windows startup.
    set_tests_properties(crawler.cli_crawl PROPERTIES TIMEOUT 40
        ENVIRONMENT "HFS_TEST_PROCESS_TIMEOUT=30")

    add_executable(crawl_test tests/crawl_test.cpp)
    target_link_libraries(crawl_test PRIVATE crawler_core GTest::gtest_main)
    target_compile_options(crawl_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME crawler.crawl COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/http_fixture.py" "$<TARGET_FILE:crawl_test>")
    set_tests_properties(crawler.crawl PROPERTIES TIMEOUT 20)

    add_executable(http_client_test tests/http_client_test.cpp tests/robots_loading_test.cpp)
    target_link_libraries(http_client_test PRIVATE crawler_core GTest::gtest_main)
    target_compile_options(http_client_test PRIVATE -Wall -Wextra -Wpedantic)
    # The Python wrapper owns the local server and forwards the test exit code.
    add_test(NAME crawler.http_client COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/http_fixture.py" "$<TARGET_FILE:http_client_test>")
    set_tests_properties(crawler.http_client PROPERTIES TIMEOUT 20)

    add_executable(curl_test tests/curl_test.cpp)
    target_link_libraries(curl_test PRIVATE CURL::libcurl_static GTest::gtest_main)
    target_compile_options(curl_test PRIVATE -Wall -Wextra -Wpedantic)
    gtest_discover_tests(curl_test TEST_PREFIX "crawler." DISCOVERY_TIMEOUT 15 PROPERTIES TIMEOUT 10)

    add_executable(url_test tests/url_test.cpp)
    target_link_libraries(url_test PRIVATE crawler_core GTest::gtest_main)
    target_compile_options(url_test PRIVATE -Wall -Wextra -Wpedantic)
    gtest_discover_tests(url_test TEST_PREFIX "crawler." DISCOVERY_TIMEOUT 15 PROPERTIES TIMEOUT 10)

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
