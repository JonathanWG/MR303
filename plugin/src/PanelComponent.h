#pragma once

#include <cstdint>

#include <juce_gui_basics/juce_gui_basics.h>

#include "sp303/ResampleRecorder.h"

#include "Skin.h"

class Sp303AudioProcessor;

// ---------------------------------------------------------------------------
// The interactive panel.
//
// Draws the skin's artwork, then paints live state ON TOP of it: lit pads, knob
// pointers at their current angle, the active effect, the 3-digit display and
// the context bar.
//
// Everything the artwork cannot know is an overlay. That is what lets the same
// code drive a vector panel today and a photograph of real hardware tomorrow -
// a photo has a picture of a knob, but only the plugin knows where that knob
// is actually pointing right now.
// ---------------------------------------------------------------------------
class PanelComponent final : public juce::Component,
                             public juce::FileDragAndDropTarget,
                             private juce::Timer {
public:
    explicit PanelComponent(Sp303AudioProcessor&);
    ~PanelComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

    bool keyPressed(const juce::KeyPress&) override;
    bool keyStateChanged(bool isKeyDown) override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragMove(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    float skinAspectRatio() const noexcept { return skin_.aspectRatio(); }

private:
    void timerCallback() override;

    // --- coordinate mapping -------------------------------------------------
    juce::Rectangle<float> panelArea() const noexcept;
    juce::Point<float>     toNormalised(juce::Point<int> pixel) const noexcept;
    juce::Rectangle<float> toPixels(juce::Rectangle<float> normalised) const noexcept;

    // --- overlays -----------------------------------------------------------
    void paintFallback(juce::Graphics&);
    void paintPadOverlay(juce::Graphics&, const SkinControl&);
    void paintKnobOverlay(juce::Graphics&, const SkinControl&);
    void paintButtonOverlay(juce::Graphics&, const SkinControl&);
    void paintDisplay(juce::Graphics&, const SkinControl&);
    void paintContextBar(juce::Graphics&, const SkinControl&);

    // --- interaction --------------------------------------------------------
    void triggerPad(int padIndex);
    void releasePad(int padIndex);
    void performAction(const SkinControl&);

    // --- resample -----------------------------------------------------------
    //
    // RESAMPLE arms, a pad chooses the destination, REC starts and stops. That
    // ordering is the hardware's: the destination is picked before the take
    // exists, so a capture can never end up with nowhere to go.
    void beginResampleTargeting();
    void chooseResampleDestination(int padIndex);
    void toggleRecording();
    void abortResample();
    bool resampleInProgress() const noexcept;

    // Reads the recorder live and refreshes the painting cache. Actions must
    // not use the 30 Hz cache: arming and pressing REC can easily happen
    // inside one timer tick, and the button would look inert.
    sp303::ResampleRecorder::State refreshResampleState() noexcept;

    float parameterValue(const juce::String& paramId) const;
    void  setParameterValue(const juce::String& paramId, float value);

    static int keyToPadIndex(int keyCode) noexcept;

    Sp303AudioProcessor& processor_;
    Skin                 skin_;

    // Knob drag state. `dragStartValue_` is captured on mouse-down so the drag
    // is absolute rather than accumulating rounding error per-frame.
    const SkinControl* draggingKnob_ {nullptr};
    float              dragStartValue_ {0.0f};
    int                dragStartY_ {0};

    // Set between pressing RESAMPLE and picking a pad. While it is true a pad
    // press selects a destination instead of playing.
    bool awaitingResampleTarget_ {false};

    // Pads whose press was swallowed by destination selection, so the matching
    // release does not emit a NoteOff for a note that never started.
    std::uint8_t consumedPads_ {0};

    // Cached on the timer rather than read from paint(): paint runs far more
    // often, and the state only changes at 20-30 Hz anyway.
    sp303::ResampleRecorder::State resampleState_
        {sp303::ResampleRecorder::State::Idle};
    float resampleFill_ {0.0f};

    // Last status the processor published, so a capture that finishes while
    // the panel is idle still reaches the context bar exactly once.
    juce::String lastProcessorStatus_;

    const SkinControl* pressedControl_ {nullptr};
    const SkinControl* hoveredControl_ {nullptr};
    int                dropTargetPad_ {-1};

    // Latest telemetry from the audio thread.
    std::uint8_t padStates_ {0};
    int          activeVoices_ {0};
    float        peakL_ {0.0f}, peakR_ {0.0f};

    // Pads currently held by the computer keyboard, so a key repeat does not
    // retrigger and a key release maps back to the right pad.
    std::uint8_t keyboardPads_ {0};

    juce::String displayText_ {"---"};

    // Context bar text, in priority order: whatever is under the cursor wins,
    // then a sticky status message from the last action, then the names of
    // whatever the three CTRL knobs currently control.
    juce::String hoverText_;
    juce::String statusText_;
    juce::String knobNamesText_;

    juce::String contextText() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PanelComponent)
};
