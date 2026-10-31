include_guard(GLOBAL)

if(LSQL_ENABLE_CLANG_TIDY)
    find_program(LSQL_CLANG_TIDY_EXECUTABLE NAMES clang-tidy)
    if(NOT LSQL_CLANG_TIDY_EXECUTABLE)
        message(FATAL_ERROR "LSQL_ENABLE_CLANG_TIDY is ON but clang-tidy was not found")
    endif()
    set(CMAKE_C_CLANG_TIDY "${LSQL_CLANG_TIDY_EXECUTABLE}")
endif()

if(LSQL_ENABLE_CPPCHECK)
    find_program(LSQL_CPPCHECK_EXECUTABLE NAMES cppcheck)
    if(NOT LSQL_CPPCHECK_EXECUTABLE)
        message(FATAL_ERROR "LSQL_ENABLE_CPPCHECK is ON but cppcheck was not found")
    endif()
    set(
        CMAKE_C_CPPCHECK
        "${LSQL_CPPCHECK_EXECUTABLE}"
        --enable=warning,style,performance,portability
        --inline-suppr
        --suppress=missingIncludeSystem
        --error-exitcode=1
        --std=c23
        --check-level=exhaustive
        --quiet
    )
endif()
