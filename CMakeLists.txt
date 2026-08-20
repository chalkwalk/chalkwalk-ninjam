# chalkwalk-ninjam -- the NINJAM wire protocol, JUCE-free.
#
# Written to work BOTH nested inside a parent project and standalone, so that
# `cmake -B build && ctest` works from this directory with nothing else on the
# machine. That is not a convenience: a library that only builds inside its
# parent has not actually been extracted.

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

# C++17, not 20, and PUBLIC means this is a demand on every consumer -- so it
# is the lowest this library actually needs. It briefly required C++20 for
# std::endian, which cost antiphon a language-standard bump it had not asked
# for: `concept` is an identifier there and a keyword in C++20.
target_compile_features(chalkwalk_ninjam PUBLIC cxx_std_17)
set_target_properties(chalkwalk_ninjam PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Ogg Vorbis, for the audio NINJAM actually carries.
#
# Vendored as submodules rather than found on the system, because a system
# package is not a thing you can rely on: the first CI run failed on macOS and
# Windows for exactly that reason while passing on Linux. A codec is also a
# specification you could fail to meet, which is the case where taking a
# dependency beats writing one -- so the question was only ever how to get it,
# not whether.
#
# BUILD_TESTING off before adding them: xiph's projects register their own
# tests with ctest, and a consumer's `ctest` should run this library's suite,
# not libogg's.
# Guarded, so a parent that already vendors them is REUSED rather than fought
# with. Antiphon has had its own copies for as long as it has existed, and
# adding a second set is not a version conflict -- it is a duplicate CMake
# target name, which fails the configure outright.
if(NOT TARGET ogg)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    add_subdirectory(modules/ogg EXCLUDE_FROM_ALL)
endif()
if(NOT TARGET vorbis)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    add_subdirectory(modules/vorbis EXCLUDE_FROM_ALL)
endif()
target_link_libraries(chalkwalk_ninjam PUBLIC vorbis vorbisenc ogg)

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
