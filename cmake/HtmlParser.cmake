include(FetchContent)

# Pin an immutable commit from v3.0.0, not a moving branch or tag.
set(LEXBOR_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(LEXBOR_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(LEXBOR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LEXBOR_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(lexbor
    GIT_REPOSITORY https://github.com/lexbor/lexbor.git
    GIT_TAG 2ae88a1c6b5261830eff73ee12bb3cdf805f3cfe
    SYSTEM
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(lexbor)
