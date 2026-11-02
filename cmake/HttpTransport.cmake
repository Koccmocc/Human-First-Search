include(FetchContent)

function(hfs_add_curl)
    # Keep curl's generic options local; our own tests remain enabled.
    set(BUILD_TESTING OFF)
    set(BUILD_SHARED_LIBS OFF)
    set(BUILD_STATIC_LIBS ON)
    set(BUILD_CURL_EXE OFF)
    set(BUILD_LIBCURL_DOCS OFF)
    set(BUILD_MISC_DOCS OFF)
    set(ENABLE_CURL_MANUAL OFF)
    set(CURL_DISABLE_INSTALL ON)

    # Start with HTTP/1.1 and TLS, without optional third-party dependencies.
    foreach(feature IN ITEMS CURL_ZLIB CURL_BROTLI CURL_ZSTD CURL_USE_LIBPSL
            CURL_USE_LIBSSH2 USE_LIBIDN2 USE_NGHTTP2)
        set(${feature} OFF)
    endforeach()
    if(WIN32)
        # Use Windows certificate validation instead of shipping OpenSSL DLLs.
        set(CURL_USE_SCHANNEL ON)
        set(CURL_USE_OPENSSL OFF)
    else()
        set(CURL_USE_OPENSSL ON)
    endif()

    FetchContent_Declare(curl
        GIT_REPOSITORY https://github.com/curl/curl.git
        GIT_TAG 3163ec065643ba20709f190111f42c3e1fe95273
        SYSTEM
        EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(curl)
endfunction()

hfs_add_curl()
