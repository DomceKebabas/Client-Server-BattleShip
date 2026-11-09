cmake_minimum_required(VERSION 3.10)
project(KTinklu1Lab C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

add_library(battleship_logic STATIC battleship_logic.c battleship_logic.h)

add_executable(server server.c)
target_link_libraries(server PRIVATE battleship_logic Threads::Threads)

add_executable(client client.c)
target_link_libraries(client PRIVATE battleship_logic Threads::Threads)
