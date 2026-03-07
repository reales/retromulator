cmake_minimum_required(VERSION 3.22)
project(Synthmulator_Plugin)

juce_add_plugin(Synthmulator
    COMPANY_NAME            "discoDSP"
    PLUGIN_MANUFACTURER_CODE DDSP
    PLUGIN_CODE             SmUL
    FORMATS                 VST3 AU Standalone
    PRODUCT_NAME            "Synthmulator"
    BUNDLE_ID               "com.discodsp.synthmulator"

    IS_SYNTH                TRUE
    NEEDS_MIDI_INPUT        TRUE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE

    EDITOR_WANTS_KEYBOARD_FOCUS FALSE

    VST3_CATEGORIES         "Instrument|Synth"
    AU_MAIN_TYPE            kAudioUnitType_MusicDevice

    COPY_PLUGIN_AFTER_BUILD FALSE
)

target_sources(Synthmulator PRIVATE
    HeadlessProcessor.h
    HeadlessProcessor.cpp
    MinimalController.h
    MinimalController.cpp
    SynthFactory.h
    SynthFactory.cpp
    SynthType.h
    PluginEntry.cpp
)

target_compile_features(Synthmulator PRIVATE cxx_std_17)

target_link_libraries(Synthmulator
    PRIVATE
        jucePluginLib
        synthLib
        virusLib
        mqLib
        xtLib
        n2xLib
        jeLib
        baseLib
        juce::juce_audio_processors
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_core
        juce::juce_data_structures
        juce::juce_events
        juce::juce_audio_plugin_client
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

target_compile_definitions(Synthmulator
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1
    PRIVATE
        JUCE_VST3_CAN_REPLACE_VST2=0
)
