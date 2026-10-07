cmake_minimum_required(VERSION 3.16)
project(embedded C)

add_subdirectory(../.. reaktor)

reaktor_add_app(simple SOURCES ../../samples/simple/simple.c)
