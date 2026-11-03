include(FetchContent)

set(ABSL_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ABSL_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "" FORCE)

# Abseil 20250814.2, required by Google's parser and matcher.
FetchContent_Declare(abseil
    GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
    GIT_TAG 0cf0a5c9d12cc3783363ab20f11613e69fd04c9a
    SYSTEM
    EXCLUDE_FROM_ALL
)
FetchContent_Declare(robotstxt
    GIT_REPOSITORY https://github.com/google/robotstxt.git
    GIT_TAG 22b355ff855419e6a3ff8ff09c0ad7fdb17116f9
    # Skip upstream CMake, which bootstraps unpinned dependencies via ExternalProject.
    SOURCE_SUBDIR hfs-no-upstream-cmake
)
FetchContent_MakeAvailable(abseil robotstxt)

# Build unmodified upstream code with explicit dependencies and no tools/install rules.
add_library(hfs_robots_parser STATIC "${robotstxt_SOURCE_DIR}/robots.cc")
target_include_directories(hfs_robots_parser SYSTEM PUBLIC "${robotstxt_SOURCE_DIR}")
target_link_libraries(hfs_robots_parser PUBLIC absl::base absl::strings absl::fixed_array)
