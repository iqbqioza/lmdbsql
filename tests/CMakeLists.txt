add_executable(lmdbsql_tests
    test_main.c
    test_support.c
    test_arena.c
    test_lexer.c
    test_parser.c
    test_sql.c
)
target_include_directories(lmdbsql_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(lmdbsql_tests PRIVATE lmdbsql)
lmdbsql_set_warnings(lmdbsql_tests)

add_test(NAME unit COMMAND lmdbsql_tests)
set_tests_properties(unit PROPERTIES ENVIRONMENT "LC_ALL=C")

if(TARGET lmdbsql_cli)
    add_test(
        NAME cli
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/cli_test.sh $<TARGET_FILE:lmdbsql_cli>
    )
    set_tests_properties(cli PROPERTIES ENVIRONMENT "LC_ALL=C")
endif()
