# Catch2, guarded so a parent that already provides it is reused rather than
# fetching a second copy.
if(NOT TARGET Catch2::Catch2WithMain)
    include(FetchContent)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.7.1)
    FetchContent_MakeAvailable(Catch2)
endif()

add_executable(chalkwalk_ninjam_tests
    NinjamProtocolTests.cpp
    VorbisCodecTests.cpp
    Sha1Tests.cpp
    IntervalClockTests.cpp
    SpscRingTests.cpp
    ChannelMixTests.cpp
    RoomConventionsTests.cpp)

target_link_libraries(chalkwalk_ninjam_tests
    PRIVATE chalkwalk::ninjam Catch2::Catch2WithMain)

# MSVC does not speak GCC's warning flags, and passing them is a hard error
# rather than a warning -- so an unguarded list breaks the Windows build at the
# command line, before a single file is compiled.
if(MSVC)
    # /Zc:preprocessor -- MSVC's traditional preprocessor treats __VA_ARGS__ as
    # a single token, so the argument-count dispatch in JuceUnitShim.h picks the
    # wrong macro and `expect(cond, msg)` becomes `CHECK((cond, msg))` -- the
    # comma operator, yielding the message. It compiled on gcc and clang and
    # failed only here, which is the whole argument for building on three.
    target_compile_options(chalkwalk_ninjam_tests PRIVATE /W4 /Zc:preprocessor)
else()
    target_compile_options(chalkwalk_ninjam_tests
        PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
endif()

add_test(NAME chalkwalk_ninjam COMMAND chalkwalk_ninjam_tests)
