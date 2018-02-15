cmake_minimum_required(VERSION 3.6)

project(dir_sync)

set(CMAKE_CXX_STANDARD 14)

add_definitions(-DASIO_STANDALONE)

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" OR
    "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" OR
    "${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang")
  add_definitions(-pedantic-errors -Werror -Wall -Wextra)
else()
  add_definitions(-DASIO_HAS_STD_ADDRESSOF)
  add_definitions(-DASIO_HAS_STD_ARRAY)
  add_definitions(-DASIO_HAS_CSTDINT)
  add_definitions(-DASIO_HAS_STD_SHARED_PTR)
  add_definitions(-DASIO_HAS_STD_TYPE_TRAITS)
  add_definitions(-DASIO_HAS_STD_ATOMIC)
  add_definitions(-D_WIN32_WINNT=0x0501)
  add_definitions(/Wall /EHsc)
endif()

find_package(Threads)

include_directories(include)


include_directories($ENV{ASIO_INCLUDE_PATH})
include_directories($ENV{SPDLOG_INCLUDE_PATH})
include_directories($ENV{JSON_INCLUDE_PATH})  # should refer to the directory 'src' of json
include_directories($ENV{CLIPP_INCLUDE_PATH})

include_directories($ENV{FMT_PATH})  # should refer to the root directory of fmt
set(FMT_LIB_PATH $ENV{FMT_PATH}/build/fmt/libfmt.a)

find_package(Protobuf REQUIRED)
include_directories(${Protobuf_INCLUDE_DIRS})
include_directories(${CMAKE_CURRENT_BINARY_DIR})
file(GLOB protos protos/*.proto)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${protos})

find_package(OpenSSL REQUIRED)
include_directories(${OPENSSL_INCLUDE_DIR})

macro(create_targets)
  foreach(curr_target ${ARGV})
    file(GLOB sources src/${curr_target}/*.cpp src/*.cpp include/${curr_target}/*.h include/*.h)
    add_executable(${curr_target} ${sources} ${PROTO_SRCS} ${PROTO_HDRS})
    
    target_link_libraries(${curr_target} ${CMAKE_THREAD_LIBS_INIT} ${OPENSSL_CRYPTO_LIBRARY} ${FMT_LIB_PATH} ${PROTOBUF_LIBRARIES})
  
  endforeach(curr_target)
endmacro(create_targets)

create_targets(dir_sync_client dir_sync_server)
