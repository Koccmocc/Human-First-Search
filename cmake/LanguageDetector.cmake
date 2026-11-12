include(FetchContent)

# CLD2 (Apache-2.0), pinned to an immutable upstream commit.
FetchContent_Declare(cld2
    GIT_REPOSITORY https://github.com/CLD2Owners/cld2.git
    GIT_TAG b56fa78a2fe44ac2851bae5bf4f4693a0644da7b
    SOURCE_SUBDIR hfs-no-upstream-cmake
)
FetchContent_MakeAvailable(cld2)

# Match the compact model in upstream internal/compile_libs.sh. Dynamic table
# loading, POSIX tools and the larger alternative model are deliberately omitted.
set(hfs_cld2_sources
    cldutil.cc cldutil_shared.cc compact_lang_det.cc compact_lang_det_hint_code.cc
    compact_lang_det_impl.cc debug.cc fixunicodevalue.cc
    generated_entities.cc generated_language.cc generated_ulscript.cc
    getonescriptspan.cc lang_script.cc offsetmap.cc scoreonescriptspan.cc
    tote.cc utf8statetable.cc
    cld_generated_cjk_uni_prop_80.cc cld2_generated_cjk_compatible.cc
    cld_generated_cjk_delta_bi_4.cc generated_distinct_bi_0.cc
    cld2_generated_quadchrome_2.cc cld2_generated_deltaoctachrome.cc
    cld2_generated_distinctoctachrome.cc cld_generated_score_quad_octa_2.cc
)
list(TRANSFORM hfs_cld2_sources PREPEND "${cld2_SOURCE_DIR}/internal/")
add_library(hfs_cld2 STATIC ${hfs_cld2_sources})
target_include_directories(hfs_cld2 SYSTEM PUBLIC "${cld2_SOURCE_DIR}")
target_compile_features(hfs_cld2 PUBLIC cxx_std_20)
# Legacy tables encode negative byte values in unsigned storage; C++11+ list
# initialization rejects those implicit conversions. Keep compatibility local
# to unmodified third-party sources, as the project currently requires GCC.
target_compile_options(hfs_cld2 PRIVATE -Wno-narrowing)
