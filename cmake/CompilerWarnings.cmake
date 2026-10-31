include_guard(GLOBAL)

function(lmdbsql_set_warnings target)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(
            ${target}
            PRIVATE
                -Wall
                -Wextra
                -Wpedantic
                -Wcast-qual
                -Wformat=2
                -Wmissing-prototypes
                -Wshadow
                -Wstrict-prototypes
                -Wundef
                -Wwrite-strings
        )
        if(LSQL_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    elseif(MSVC)
        target_compile_options(${target} PRIVATE /W4)
        if(LSQL_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    endif()
endfunction()
