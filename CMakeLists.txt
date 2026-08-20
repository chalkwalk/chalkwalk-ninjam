# chalkwalk-ninjam -- the NINJAM wire protocol, JUCE-free.
#
# Written to work BOTH nested inside a parent project and standalone, so that
# `cmake -B build && ctest` works from this directory with nothing else on the
# machine. That is not a convenience: a library that only builds inside its
# parent has not actually been extracted. See ../../../ECOSYSTEM.md.

cmake_minimum_required(VERSION 3.22)
project(chalkwalk_ninjam VERSION 0.1.0 LANGUAGES CXX)

add_library(chalkwalk_ninjam STATIC
    src/NinjamProtocol.cpp
    src/Sha1.cpp
    src/IntervalClock.cpp
    src/VorbisCodec.cpp)
add_library(chalkwalk::ninjam ALIAS chalkwalk_ninjam)

target_include_directories(chalkwalk_ninjam
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_compile_features(chalkwalk_ninjam PUBLIC cxx_std_20)
set_target_properties(chalkwalk_ninjam PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Ogg Vorbis, for the audio NINJAM actually carries. The only dependency, and
# the codec is a specification you could fail to meet -- exactly the case where
# taking one beats writing one.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(VORBIS QUIET vorbis vorbisenc ogg)
endif()
if(VORBIS_FOUND)
    target_include_directories(chalkwalk_ninjam PUBLIC ${VORBIS_INCLUDE_DIRS})
    target_link_libraries(chalkwalk_ninjam PUBLIC ${VORBIS_LIBRARIES})
else()
    target_link_libraries(chalkwalk_ninjam PUBLIC vorbis vorbisenc ogg)
endif()

if(MSVC)
    target_compile_options(chalkwalk_ninjam PRIVATE /W4)
else()
    target_compile_options(chalkwalk_ninjam PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow -Wimplicit-fallthrough)
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(CHALKWALK_NINJAM_TESTS_DEFAULT ON)
else()
    set(CHALKWALK_NINJAM_TESTS_DEFAULT OFF)
endif()
option(CHALKWALK_NINJAM_TESTS "Build chalkwalk-ninjam tests"
       ${CHALKWALK_NINJAM_TESTS_DEFAULT})

if(CHALKWALK_NINJAM_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
