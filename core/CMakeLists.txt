add_library(sp303_core STATIC
    src/dsp/Interpolator.cpp
    src/dsp/Biquad.cpp
    src/dsp/Saturator.cpp
    src/dsp/Quantizer.cpp
    src/dsp/Decimator.cpp
    src/dsp/Fir.cpp
    src/dsp/Oversampler.cpp
    src/dsp/DelayLine.cpp
    src/fx/FilterDrive.cpp
    src/fx/Pitch.cpp
    src/fx/Delay.cpp
    src/fx/VinylSim.cpp
    src/fx/Isolator.cpp
    src/fx/Reverb.cpp
    src/fx/TapeEcho.cpp
    src/fx/Chorus.cpp
    src/fx/Flanger.cpp
    src/fx/Phaser.cpp
    src/fx/EffectRack.cpp
    src/voice/Voice.cpp
    src/voice/VoiceManager.cpp
    src/sequencer/Quantize.cpp
    src/sequencer/Sequencer.cpp
    src/SampleBuffer.cpp
    src/ResampleRecorder.cpp
    src/Device.cpp
)

add_library(sp303::core ALIAS sp303_core)

target_include_directories(sp303_core
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(sp303_core PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(sp303_core PRIVATE /W4 $<$<BOOL:${SP303_WERROR}>:/WX>)
else()
    target_compile_options(sp303_core PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow
        $<$<BOOL:${SP303_WERROR}>:-Werror>)
endif()

# ----------------------------------------------------------------------------
# Real-time safety.
#
# The audio thread must never allocate, lock, or do I/O. RealtimeSanitizer
# (clang 20+) enforces that at runtime for functions marked [[clang::nonblocking]].
# See core/include/sp303/rt/RtSafe.h
# ----------------------------------------------------------------------------
if(SP303_RT_SANITIZE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(sp303_core PUBLIC -fsanitize=realtime)
        target_link_options(sp303_core    PUBLIC -fsanitize=realtime)
        target_compile_definitions(sp303_core PUBLIC SP303_RT_SANITIZE=1)
    else()
        message(WARNING "SP303_RT_SANITIZE requires clang; ignoring.")
    endif()
endif()
