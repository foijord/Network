cmake_minimum_required (VERSION 3.21.1)
project(Network LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

find_package(Boost REQUIRED)

include_directories(
	${Boost_INCLUDE_DIRS}
	${CMAKE_SOURCE_DIR}
	${CMAKE_SOURCE_DIR}/../)

link_directories(${Boost_LIBRARY_DIRS})

add_compile_definitions($<$<CONFIG:Debug>:DEBUG>)

add_executable(server server.cpp connection.h message.h)
target_link_libraries (server ${Boost_LIBRARIES})

add_executable(client client.cpp connection.h message.h)
target_link_libraries (client ${Boost_LIBRARIES})
