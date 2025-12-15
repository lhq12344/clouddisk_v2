cmake_minimum_required(VERSION 3.16)
project(email_srv LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(EMAIL_SRV_SOURCES
    main.cpp
    config/config.cpp
    log/Logger.cpp
)

add_executable(${PROJECT_NAME} ${EMAIL_SRV_SOURCES})

# Local project includes
target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/config
        ${CMAKE_CURRENT_SOURCE_DIR}/log
)

find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        CURL::libcurl
        Threads::Threads
)

# librdkafka (Kafka consumer)
find_path(RDKAFKA_INCLUDE_DIR
    NAMES librdkafka/rdkafka.h
    PATHS /usr/include /usr/local/include
)
find_library(RDKAFKA_LIBRARY
    NAMES rdkafka
    PATHS /usr/lib /usr/local/lib
)
if(NOT RDKAFKA_INCLUDE_DIR OR NOT RDKAFKA_LIBRARY)
    message(FATAL_ERROR "librdkafka headers or library not found. Please install librdkafka-dev.")
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${RDKAFKA_INCLUDE_DIR})
target_link_libraries(${PROJECT_NAME} PRIVATE ${RDKAFKA_LIBRARY})

# hiredis (Redis pool)
find_path(HIREDIS_INCLUDE_DIR
    NAMES hiredis/hiredis.h
    PATHS /usr/include /usr/local/include
)
find_library(HIREDIS_LIBRARY
    NAMES hiredis
    PATHS /usr/lib /usr/local/lib
)
if(NOT HIREDIS_INCLUDE_DIR OR NOT HIREDIS_LIBRARY)
    message(FATAL_ERROR "hiredis headers or library not found. Please install hiredis-dev.")
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${HIREDIS_INCLUDE_DIR})
target_link_libraries(${PROJECT_NAME} PRIVATE ${HIREDIS_LIBRARY})

# Nacos SDK C++ (configuration service)
find_path(NACOS_INCLUDE_DIR
    NAMES Nacos.h
    PATHS /usr/local/include /usr/include /usr/local/include/nacos /usr/include/nacos
)
find_library(NACOSCPP_LIB
    NAMES nacos-cli
    PATHS /usr/local/lib /usr/lib
)
if(NACOS_INCLUDE_DIR AND NACOSCPP_LIB)
    target_include_directories(${PROJECT_NAME} PRIVATE ${NACOS_INCLUDE_DIR})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${NACOSCPP_LIB})
else()
    message(WARNING "Nacos C++ SDK not found. Build will fail if the headers and library are unavailable at compile time.")
endif()
