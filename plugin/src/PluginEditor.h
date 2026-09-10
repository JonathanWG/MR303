#pragma once

#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "PanelComponent.h"
#include "PluginProcessor.h"

// ---------------------------------------------------------------------------
// The editor is now a thin host for PanelComponent.
//
// All the interaction lives in the panel, because the panel is what the skin
// describes. Keeping the editor empty means the window chrome, sizing and
// constrainer are the only things that change if the skin changes.
// ---------------------------------------------------------------------------
class Sp303AudioProcessorEditor final : public juce::AudioProcessorEditor {
public:
    explicit Sp303AudioProcessorEditor(Sp303AudioProcessor&);
    ~Sp303AudioProcessorEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    Sp303AudioProcessor& processor_;
    PanelComponent       panel_;

    // Locks the window to the artwork's proportions while resizing. Without
    // it a photographic panel gets stretched, which reads as broken
    // immediately.
    juce::ComponentBoundsConstrainer constrainer_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Sp303AudioProcessorEditor)
};
