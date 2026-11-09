include(FetchContent)

set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)

# nlohmann/json v3.12.0 (MIT), pinned to the release's immutable commit.
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG 55f93686c01528224f448c19128836e7df245f72
    SYSTEM
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(nlohmann_json)
