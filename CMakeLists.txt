cmake_minimum_required(VERSION 3.20)

project(redis)

set(CMAKE_C_STANDARD 17)

add_executable(main
    main
)

target_compile_options(main PRIVATE
    -Wall
    -Wextra)

# target_link_libraries(main PRIVATE // add libraries such as pthread)