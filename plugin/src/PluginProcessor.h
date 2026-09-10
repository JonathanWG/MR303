#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "sp303/Device.h"

// ---------------------------------------------------------------------------
// Layer 1-4: the JUCE wrapper.
//
// This class deliberately holds almost NO logic. Its whole job is to translate
// between the host's world (buffers, MIDI, transport, parameter automation,
// state serialisation) and sp303::Device, which knows nothing about plugins.
//
// That separation is what lets the DSP core be tested headless and be driven
// by the Python null-test harness without a DAW in the loop.
// ---------------------------------------------------------------------------
class Sp303AudioProcessor final : public juce::AudioProcessor,
                                  private juce::Timer {
public:
    Sp303AudioProcessor();
    ~Sp303AudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "SP303"; }
    bool acceptsMidi()  const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    sp303::Device& device() noexcept { return device_; }
    juce::AudioProcessorValueTreeState& parameters() noexcept { return params_; }

    // Called from the UI thread. Decodes the file on a background thread and
    // hands the result to the audio thread via SampleSlot's atomic swap.
    void loadSampleIntoSlot(int slotIndex, const juce::File& file);

    // --- panel accessors ------------------------------------------------------
    //
    // The GUI reads and writes selection state through these rather than
    // touching the APVTS directly, so every change goes through the
    // host-visible parameter and is captured for automation and undo.
    int  currentEffectIndex()  const noexcept;
    int  currentBankIndex()    const noexcept;
    int  currentQualityIndex() const noexcept;

    void setEffectIndex(int index);
    void setBankIndex(int index);
    void setQualityIndex(int index);

    // --- resample -------------------------------------------------------------
    void armResample(int destinationSlot);
    void startResample();
    void stopResample();
    void cancelResample();

    // Set when a capture completes, for the panel to display. Cleared on the
    // next arm.
    juce::String resampleStatus() const;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void handleMidi(const juce::MidiBuffer& midi);
    sp303::seq::TransportInfo readTransport();

    // Runs on the message thread. Collects a finished resample and turns it
    // into a pad sample. Lives on the processor, not the editor, so a capture
    // that completes with the window closed is still picked up.
    void timerCallback() override;
    void collectFinishedResample();

    // Audio embedded in the project state is FLAC-compressed. Lossless, and
    // roughly half the size of raw PCM.
    static bool encodeSample(const sp303::SampleBuffer&, juce::MemoryBlock&);
    static sp303::SampleBufferPtr decodeSample(const juce::MemoryBlock&,
                                               double sampleRate,
                                               const juce::String& name);

    sp303::Device                      device_;
    juce::AudioProcessorValueTreeState params_;
    juce::AudioFormatManager           formatManager_;

    juce::CriticalSection statusLock_;
    juce::String          resampleStatus_;

    // MIDI note that maps to pad 0. Pads occupy 8 consecutive notes from here.
    // 36 = C1, which is where most pad controllers put their bottom-left pad.
    int padBaseNote_ {36};

    double lastKnownPpq_ {-1.0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Sp303AudioProcessor)
};
