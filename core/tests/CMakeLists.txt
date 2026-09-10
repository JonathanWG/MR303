include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.2
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(Catch2)

add_executable(sp303_tests
    test_interpolator.cpp
    test_spsc_queue.cpp
    test_biquad.cpp
    test_smoother.cpp
    test_voice.cpp
    test_quantize.cpp
    test_sample_buffer.cpp
    test_resample_recorder.cpp
    test_effects.cpp
    test_reclamation.cpp
)

target_link_libraries(sp303_tests
    PRIVATE
        sp303::core
        Catch2::Catch2WithMain
)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(sp303_tests)
