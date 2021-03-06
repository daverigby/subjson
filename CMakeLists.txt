CMAKE_MINIMUM_REQUIRED(VERSION 2.8.9)
PROJECT(subjson)
INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR})

SET(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
SET(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)
IF(WIN32)
    SET(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
    ADD_DEFINITIONS(-D_CRT_SECURE_NO_WARNINGS)
ELSE()
    SET(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)
ENDIF()

FILE(GLOB SUBJSON_SRC subdoc/*.c subdoc/*.cc)
ADD_LIBRARY(subjson ${SUBJSON_SRC})
ADD_EXECUTABLE(bench bench.cc contrib/cliopts/cliopts.c)
TARGET_LINK_LIBRARIES(bench subjson)

ENABLE_TESTING()
ADD_SUBDIRECTORY(tests)
