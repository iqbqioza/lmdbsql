cmake_minimum_required(VERSION 3.21)

project(
    lmdbsql
    VERSION 0.1.0
    DESCRIPTION "An embedded SQL engine backed by LMDB"
    LANGUAGES C
)

include(CTest)
include(GNUInstallDirs)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

option(LSQL_BUILD_CLI "Build the lmdbsql command line shell" ON)
option(LSQL_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(LSQL_ENABLE_SANITIZERS "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(LSQL_ENABLE_CLANG_TIDY "Run clang-tidy as part of the build" OFF)
option(LSQL_ENABLE_CPPCHECK "Run cppcheck as part of the build" OFF)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CompilerWarnings)
include(Sanitizers)
include(StaticAnalysis)

find_package(LMDB 0.9 REQUIRED)

lmdbsql_enable_sanitizers()

add_library(lmdbsql
    src/arena.c
    src/engine.c
    src/error.c
    src/lexer.c
    src/lmdbsql.c
    src/parser.c
    src/result.c
    src/row.c
    src/schema.c
    src/value.c
)
add_library(lmdbsql::lmdbsql ALIAS lmdbsql)

target_include_directories(lmdbsql
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(lmdbsql PRIVATE LMDB::LMDB)
lmdbsql_set_warnings(lmdbsql)

if(LSQL_BUILD_CLI)
    add_executable(lmdbsql_cli tools/lmdbsql.c)
    set_target_properties(lmdbsql_cli PROPERTIES OUTPUT_NAME lmdbsql)
    target_link_libraries(lmdbsql_cli PRIVATE lmdbsql)
    target_compile_definitions(lmdbsql_cli PRIVATE LSQL_VERSION="${PROJECT_VERSION}")
    lmdbsql_set_warnings(lmdbsql_cli)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

install(
    TARGETS lmdbsql
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
if(LSQL_BUILD_CLI)
    install(TARGETS lmdbsql_cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
