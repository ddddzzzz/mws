cmake_minimum_required (VERSION 2.6)
project (final)

ADD_DEFINITIONS(
    -std=c++11 # Or -std=c++0x
)

add_executable(final main.cpp Acceptor.cpp Connection.cpp)
target_link_libraries(final pthread ev)
