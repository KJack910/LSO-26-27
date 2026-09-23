cmake_minimum_required(VERSION 3.15)

project(battleship C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(Threads REQUIRED)
add_executable(server server.c server_state.c common.c game.c)
target_link_libraries(server PRIVATE Threads::Threads)
add_executable(client client.c client_net.c client_ui.c common.c)

include(CTest)
if(BUILD_TESTING)
    add_executable(test_game tests/test_game.c game.c)
    target_include_directories(test_game PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME game_rules COMMAND test_game)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME server_protocol
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_server.py $<TARGET_FILE:server> $<TARGET_FILE:client>)
    endif()
endif()

if(WIN32)
    target_link_libraries(server PRIVATE ws2_32)
    target_link_libraries(client PRIVATE ws2_32)
endif()

if(MSVC)
    target_compile_options(server PRIVATE /W4)
    target_compile_options(client PRIVATE /W4)
else()
    target_compile_options(server PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(client PRIVATE -Wall -Wextra -Wpedantic)
endif()
