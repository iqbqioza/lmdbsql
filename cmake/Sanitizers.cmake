include_guard(GLOBAL)

function(lmdbsql_enable_sanitizers)
    if(NOT LSQL_ENABLE_SANITIZERS)
        return()
    endif()

    if(NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "LSQL_ENABLE_SANITIZERS requires GCC or Clang")
    endif()

    message(STATUS "Enabling AddressSanitizer and UndefinedBehaviorSanitizer")
    add_compile_options(
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
        -fno-sanitize-recover=all
    )
    add_link_options(-fsanitize=address,undefined)
endfunction()
