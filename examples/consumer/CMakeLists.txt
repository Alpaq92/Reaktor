cmake_minimum_required(VERSION 3.16)
project(consumer C)

find_package(reaktor 0.2 REQUIRED)

add_executable(simple WIN32 ../../samples/simple/simple.c)
target_link_libraries(simple PRIVATE reaktor::reaktor)
if(EMSCRIPTEN)
    set_target_properties(simple PROPERTIES SUFFIX ".html")
endif()
