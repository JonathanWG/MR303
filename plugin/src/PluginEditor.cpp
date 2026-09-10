#include "PluginEditor.h"

using namespace juce;

namespace {
constexpr int kDefaultWidth = 1100;
}

Sp303AudioProcessorEditor::Sp303AudioProcessorEditor(Sp303AudioProcessor& processor)
    : AudioProcessorEditor(&processor), processor_(processor), panel_(processor) {

    addAndMakeVisible(panel_);

    const double aspect = static_cast<double>(panel_.skinAspectRatio());

    constrainer_.setFixedAspectRatio(aspect);
    constrainer_.setSizeLimits(640, static_cast<int>(640.0 / aspect),
                               2200, static_cast<int>(2200.0 / aspect));
    setConstrainer(&constrainer_);

    setResizable(true, true);
    setSize(kDefaultWidth, static_cast<int>(kDefaultWidth / aspect));

    // The panel handles pad keys, so it needs focus as soon as the window opens.
    panel_.grabKeyboardFocus();
}

void Sp303AudioProcessorEditor::paint(Graphics& g) {
    g.fillAll(Colour(0xff12100c));
}

void Sp303AudioProcessorEditor::resized() {
    panel_.setBounds(getLocalBounds());
}
