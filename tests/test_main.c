#include "test_harness.h"

#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 202311L
#error "lmdbsql requires a C23 compiler"
#endif

int main(void) {
    test_begin("arena");
    run_arena_tests();
    test_begin("lexer");
    run_lexer_tests();
    test_begin("parser");
    run_parser_tests();
    test_begin("sql");
    run_sql_tests();

    if (test_failures == 0) {
        printf("All tests passed.\n");
        return EXIT_SUCCESS;
    }

    fprintf(stderr, "%d test check(s) failed.\n", test_failures);
    return EXIT_FAILURE;
}
