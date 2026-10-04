include(FetchContent)
# Immutable upstream revisions. Local overrides work offline after bootstrap.
foreach(dep cordis raylib raygui)
    if(EXISTS "${PROJECT_SOURCE_DIR}/.deps/${dep}/CMakeLists.txt" OR
       EXISTS "${PROJECT_SOURCE_DIR}/.deps/${dep}/src/raygui.h")
        string(TOUPPER "${dep}" upper_dep)
        set(FETCHCONTENT_SOURCE_DIR_${upper_dep} "${PROJECT_SOURCE_DIR}/.deps/${dep}")
    endif()
endforeach()
if(EXISTS "${PROJECT_SOURCE_DIR}/.deps/json/CMakeLists.txt")
    set(FETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON "${PROJECT_SOURCE_DIR}/.deps/json")
endif()
FetchContent_Declare(nlohmann_json GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG 55f93686c01528224f448c19128836e7df245f72)
FetchContent_MakeAvailable(nlohmann_json)
set(CORDIS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CORDIS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(cordis GIT_REPOSITORY https://github.com/luoyebai/cordis-cpp.git
    GIT_TAG 8eba297c535894ebe9b924ecb5e1046c0963d960)
FetchContent_MakeAvailable(cordis)
if(MSVC)
    target_compile_options(cordis_core PRIVATE /utf-8)
    target_compile_options(cordis_loader PRIVATE /utf-8)
endif()
if(CARDIS_BUILD_CLIENT)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(raylib GIT_REPOSITORY https://github.com/raysan5/raylib.git
        GIT_TAG c1ab645ca298a2801097931d1079b10ff7eb9df8)
    FetchContent_Declare(raygui GIT_REPOSITORY https://github.com/raysan5/raygui.git
        GIT_TAG 25c8c65a6e5f0f4d4b564a0343861898c6f2778b)
    FetchContent_MakeAvailable(raylib raygui)
endif()
