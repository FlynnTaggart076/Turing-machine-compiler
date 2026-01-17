cmake_minimum_required(VERSION 3.21)

project(TuringMachine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SFML_DIR "${CMAKE_SOURCE_DIR}/include/SFML-3.0.2/lib/cmake/SFML" CACHE PATH "Path to SFMLConfig.cmake")
find_package(SFML 3 REQUIRED COMPONENTS Graphics Window System Main)

add_executable(turing_machine
    src/App.cpp
    src/Lexer.cpp
    src/Condition.cpp
    src/IR.cpp
    src/Flatten.cpp
    src/CodegenPrimitives.cpp
    src/TransitionGenerator.cpp
    src/Compiler.cpp
    src/Interpreter.cpp
    src/TransitionTable.cpp
    src/TuringMachine.cpp
    src/main.cpp
)

target_include_directories(turing_machine PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/SFML-3.0.2/include
)

target_link_libraries(turing_machine PRIVATE SFML::Graphics SFML::Window SFML::System SFML::Main)

if(MSVC)
    target_compile_options(turing_machine PRIVATE /W4 /permissive-)
else()
    target_compile_options(turing_machine PRIVATE -Wall -Wextra -pedantic)
endif()
