include(FetchContent)

# ---------------------------------------------------------------------------
# JUCE
#
# LICENSING: JUCE is dual-licensed (GPLv3 or commercial) and the VST3 SDK
# carries its own Steinberg agreement. Both must be settled BEFORE this target
# ships. See ../docs/LICENSING.md - this is a blocking item, not paperwork to
# sort out later.
# ---------------------------------------------------------------------------
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(sp303_plugin
    PRODUCT_NAME             "SP303"          # placeholder - see docs/LICENSING.md
    COMPANY_NAME             "YourCompany"
    BUNDLE_ID                "com.yourcompany.sp303"

    PLUGIN_MANUFACTURER_CODE Ycmp
    PLUGIN_CODE              Sp33

    FORMATS                  VST3 AU Standalone

    IS_SYNTH                 TRUE
    NEEDS_MIDI_INPUT         TRUE
    NEEDS_MIDI_OUTPUT        FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD  TRUE
)

target_sources(sp303_plugin
    PRIVATE
        src/PluginProcessor.cpp
        src/PluginEditor.cpp
        src/PanelComponent.cpp
        src/Skin.cpp
)

# ---------------------------------------------------------------------------
# Skins
#
# During development the panel is loaded from disk next to the binary, so the
# artwork and hitboxes can be regenerated and reloaded without a rebuild.
#
# TODO(phase-5): embed with juce_add_binary_data before release - a shipped
# plugin must not depend on loose files sitting beside the .vst3.
# ---------------------------------------------------------------------------
add_custom_command(TARGET sp303_plugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/../resources/skins
            $<TARGET_FILE_DIR:sp303_plugin>/resources/skins
    COMMENT "Copying panel skins next to the binary"
    VERBATIM
)

target_compile_definitions(sp303_plugin
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0   # requires a paid JUCE licence
)

target_link_libraries(sp303_plugin
    PRIVATE
        sp303::core
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
