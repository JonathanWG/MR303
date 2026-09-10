#include "PanelComponent.h"

#include "PluginProcessor.h"

using namespace juce;

namespace {

const Colour kAmber     {0xffe0a458};
const Colour kAmberDim  {0xff8a6533};
const Colour kCream     {0xfff2e9da};
const Colour kMuted     {0xffa89a84};
const Colour kGreen     {0xff7a9e8e};
const Colour kBackdrop  {0xff12100c};
const Colour kDisplayBg {0xff0d0b08};

// Vertical pixels of drag for a knob's full 0..1 travel. Tuned so a
// comfortable forearm movement covers the range without feeling twitchy.
constexpr float kKnobDragRange = 220.0f;
constexpr float kKnobFineRatio = 0.2f;   // with shift held

// Knob sweep, matching how the artwork draws its pointer: 12 o'clock is
// centre, 270 degrees total travel.
constexpr float kKnobStartAngle = MathConstants<float>::pi * 1.25f;
constexpr float kKnobEndAngle   = MathConstants<float>::pi * 2.75f;

// Home-row-ish mapping: two rows of four, matching the pad grid on screen.
constexpr int kPadKeys[sp303::kNumPads] {
    'Z', 'X', 'C', 'V', 'A', 'S', 'D', 'F'
};

}  // namespace

// ---------------------------------------------------------------------------

PanelComponent::PanelComponent(Sp303AudioProcessor& processor)
    : processor_(processor) {

    // Skins live next to the binary during development. A real build embeds
    // them via juce_add_binary_data - see the TODO in the CMake target.
    const auto skinDirectory =
        File::getSpecialLocation(File::currentExecutableFile)
            .getParentDirectory()
            .getChildFile("resources/skins/default");

    if (!skin_.load(skinDirectory)) {
        // Not fatal: paintFallback() draws a usable panel so the plugin is
        // still playable. An empty window would be far worse than an ugly one.
        DBG("Skin failed to load: " << skin_.errorMessage());
    }

    setWantsKeyboardFocus(true);
    startTimerHz(30);
}

PanelComponent::~PanelComponent() {
    stopTimer();
}

// --- coordinate mapping ----------------------------------------------------

Rectangle<float> PanelComponent::panelArea() const noexcept {
    // Letterbox to the skin's aspect ratio. Stretching a photograph of
    // hardware to an arbitrary window shape looks immediately wrong, and the
    // hitboxes would no longer match what the user sees.
    const auto area = getLocalBounds().toFloat();
    const float target = skin_.aspectRatio();

    if (area.getHeight() <= 0.0f || target <= 0.0f) return area;

    const float current = area.getWidth() / area.getHeight();
    if (current > target) {
        const float width = area.getHeight() * target;
        return area.withSizeKeepingCentre(width, area.getHeight());
    }
    const float height = area.getWidth() / target;
    return area.withSizeKeepingCentre(area.getWidth(), height);
}

Point<float> PanelComponent::toNormalised(Point<int> pixel) const noexcept {
    const auto area = panelArea();
    if (area.getWidth() <= 0.0f || area.getHeight() <= 0.0f) return {-1.0f, -1.0f};

    return {(static_cast<float>(pixel.x) - area.getX()) / area.getWidth(),
            (static_cast<float>(pixel.y) - area.getY()) / area.getHeight()};
}

Rectangle<float> PanelComponent::toPixels(Rectangle<float> normalised) const noexcept {
    const auto area = panelArea();
    return {area.getX() + normalised.getX() * area.getWidth(),
            area.getY() + normalised.getY() * area.getHeight(),
            normalised.getWidth() * area.getWidth(),
            normalised.getHeight() * area.getHeight()};
}

// --- painting --------------------------------------------------------------

void PanelComponent::paint(Graphics& g) {
    g.fillAll(kBackdrop);

    if (!skin_.isValid()) {
        paintFallback(g);
        return;
    }

    const auto area = panelArea();
    skin_.drawBackground(g, area);

    for (const auto& control : skin_.controls()) {
        switch (control.type) {
            case SkinControl::Type::Pad:     paintPadOverlay(g, control);    break;
            case SkinControl::Type::Knob:    paintKnobOverlay(g, control);   break;
            case SkinControl::Type::Button:
            case SkinControl::Type::Toggle:  paintButtonOverlay(g, control); break;
            case SkinControl::Type::Display: paintDisplay(g, control);       break;
            case SkinControl::Type::Label:   paintContextBar(g, control);    break;
            default: break;
        }
    }
}

void PanelComponent::paintFallback(Graphics& g) {
    // Drawn, not loaded - this must work when the skin is missing entirely.
    auto area = getLocalBounds().reduced(20);

    g.setColour(kAmber);
    g.setFont(FontOptions(20.0f, Font::bold));
    g.drawText("SKIN NOT LOADED", area.removeFromTop(30), Justification::centred);

    g.setColour(kMuted);
    g.setFont(FontOptions(13.0f));
    g.drawFittedText(skin_.errorMessage().isNotEmpty()
                         ? skin_.errorMessage()
                         : "resources/skins/default not found next to the binary",
                     area.removeFromTop(40), Justification::centred, 2);

    area.removeFromTop(20);

    // A minimal playable pad grid, so the engine can still be driven.
    const int cols = 4, rows = 2;
    const int w = area.getWidth() / cols;
    const int h = juce::jmin(area.getHeight() / rows, 110);

    for (int i = 0; i < sp303::kNumPads; ++i) {
        Rectangle<int> pad(area.getX() + (i % cols) * w,
                           area.getY() + (i / cols) * h, w, h);
        pad = pad.reduced(5);

        const bool lit = (padStates_ & (1u << i)) != 0;
        g.setColour(lit ? kAmberDim : Colour(0xff2a231a));
        g.fillRoundedRectangle(pad.toFloat(), 6.0f);
        g.setColour(kAmber.withAlpha(0.5f));
        g.drawRoundedRectangle(pad.toFloat(), 6.0f, 1.5f);
        g.setColour(kCream);
        g.drawText(String(i + 1), pad, Justification::centred);
    }
}

void PanelComponent::paintPadOverlay(Graphics& g, const SkinControl& control) {
    const auto bounds = toPixels(control.bounds);
    const bool lit     = control.index >= 0
                      && (padStates_ & (1u << control.index)) != 0;
    const bool pressed = pressedControl_ == &control;
    const bool dropTarget = dropTargetPad_ == control.index;

    if (lit || pressed) {
        // Additive glow rather than an opaque fill: over a photograph the
        // underlying pad must still read through.
        g.setColour(kAmber.withAlpha(pressed ? 0.55f : 0.35f));
        g.fillRoundedRectangle(bounds, bounds.getWidth() * 0.07f);
    }

    if (dropTarget || awaitingResampleTarget_) {
        // Same green as a drag-and-drop target, and for the same reason: in
        // both cases the pad is about to receive audio rather than play it.
        g.setColour(kGreen.withAlpha(dropTarget ? 1.0f : 0.75f));
        g.drawRoundedRectangle(bounds.reduced(2.0f), bounds.getWidth() * 0.07f, 3.0f);
    } else if (hoveredControl_ == &control) {
        g.setColour(kAmber.withAlpha(0.35f));
        g.drawRoundedRectangle(bounds.reduced(1.0f), bounds.getWidth() * 0.07f, 1.5f);
    }
}

void PanelComponent::paintKnobOverlay(Graphics& g, const SkinControl& control) {
    const auto bounds = toPixels(control.bounds);
    const auto centre = bounds.getCentre();
    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;

    const float value = parameterValue(control.param);
    const float angle = kKnobStartAngle + value * (kKnobEndAngle - kKnobStartAngle);

    // The artwork draws a pointer at 12 o'clock; mask it before drawing the
    // live one, otherwise every knob appears to have two indicators.
    g.setColour(Colour(0xff3a3128));
    g.fillEllipse(Rectangle<float>(radius * 1.24f, radius * 1.24f)
                      .withCentre(centre));

    Path pointer;
    pointer.startNewSubPath(0.0f, -radius * 0.30f);
    pointer.lineTo(0.0f, -radius * 0.84f);

    g.setColour(draggingKnob_ == &control ? kCream : kAmber);
    g.strokePath(pointer,
                 PathStrokeType(juce::jmax(2.0f, radius * 0.10f),
                                PathStrokeType::curved, PathStrokeType::rounded),
                 AffineTransform::rotation(angle).translated(centre));

    // Arc showing travel so far - reads at a glance during a filter sweep.
    Path arc;
    arc.addCentredArc(centre.x, centre.y, radius * 0.94f, radius * 0.94f,
                      0.0f, kKnobStartAngle, angle, true);
    g.setColour(kAmber.withAlpha(0.55f));
    g.strokePath(arc, PathStrokeType(juce::jmax(1.5f, radius * 0.06f)));
}

void PanelComponent::paintButtonOverlay(Graphics& g, const SkinControl& control) {
    const auto bounds = toPixels(control.bounds);

    // Effect and bank buttons are radio groups: highlight the selected one by
    // comparing the parameter's index against this button's payload.
    bool active = false;
    if (control.action == "selectEffect")
        active = processor_.currentEffectIndex() == control.value;
    else if (control.action == "selectBank")
        active = processor_.currentBankIndex() == control.value;
    else if (control.action == "resample")
        active = awaitingResampleTarget_ || resampleInProgress();
    else if (control.action == "rec")
        active = resampleState_ == sp303::ResampleRecorder::State::Armed
              || resampleInProgress();

    if (active) {
        g.setColour(kAmber.withAlpha(0.42f));
        g.fillRoundedRectangle(bounds, 4.0f);
        g.setColour(kAmber);
        g.drawRoundedRectangle(bounds.reduced(1.0f), 4.0f, 2.0f);
    }

    if (pressedControl_ == &control) {
        g.setColour(kCream.withAlpha(0.28f));
        g.fillRoundedRectangle(bounds, 4.0f);
    } else if (hoveredControl_ == &control) {
        g.setColour(kAmber.withAlpha(0.30f));
        g.drawRoundedRectangle(bounds.reduced(1.0f), 4.0f, 1.5f);
    }
}

void PanelComponent::paintDisplay(Graphics& g, const SkinControl& control) {
    const auto bounds = toPixels(control.bounds);

    // Cover the artwork's placeholder digits before drawing the live value.
    g.setColour(kDisplayBg);
    g.fillRoundedRectangle(bounds.reduced(3.0f), 4.0f);

    g.setColour(kAmber);
    g.setFont(FontOptions(bounds.getHeight() * 0.58f, Font::bold));
    g.drawText(displayText_, bounds, Justification::centred);
}

void PanelComponent::paintContextBar(Graphics& g, const SkinControl& control) {
    const auto bounds = toPixels(control.bounds);

    g.setColour(Colour(0xff2a231a));
    g.fillRoundedRectangle(bounds.reduced(1.0f), 3.0f);

    // The whole reason a 3-digit display is tolerable in 2026: it stays
    // faithful, and this line says in plain language what the knobs do right
    // now. Authentic mode can hide it.
    g.setColour(hoverText_.isNotEmpty() ? kCream : kMuted);
    g.setFont(FontOptions(juce::jmax(10.0f, bounds.getHeight() * 0.52f)));
    g.drawText(contextText(), bounds.reduced(8.0f, 0.0f), Justification::centredLeft);
}

juce::String PanelComponent::contextText() const {
    if (hoverText_.isNotEmpty())  return hoverText_;
    if (statusText_.isNotEmpty()) return statusText_;
    return knobNamesText_;
}

void PanelComponent::resized() {}

// --- mouse -----------------------------------------------------------------

void PanelComponent::mouseDown(const MouseEvent& event) {
    if (!skin_.isValid()) return;

    const auto* control = skin_.hitTest(toNormalised(event.getPosition()));
    if (control == nullptr) return;

    pressedControl_ = control;

    switch (control->type) {
        case SkinControl::Type::Pad:
            triggerPad(control->index);
            break;

        case SkinControl::Type::Knob:
            draggingKnob_   = control;
            dragStartValue_ = parameterValue(control->param);
            dragStartY_     = event.getPosition().y;
            break;

        case SkinControl::Type::Button:
        case SkinControl::Type::Toggle:
            performAction(*control);
            break;

        default:
            break;
    }
    repaint();
}

void PanelComponent::mouseDrag(const MouseEvent& event) {
    if (draggingKnob_ == nullptr) return;

    // Absolute from the drag origin, not incremental - incremental
    // accumulates error and drifts over a long sweep.
    const float delta = static_cast<float>(dragStartY_ - event.getPosition().y);
    const float sensitivity = event.mods.isShiftDown() ? kKnobFineRatio : 1.0f;
    const float value = juce::jlimit(
        0.0f, 1.0f, dragStartValue_ + (delta / kKnobDragRange) * sensitivity);

    setParameterValue(draggingKnob_->param, value);
    repaint();
}

void PanelComponent::mouseUp(const MouseEvent&) {
    if (pressedControl_ != nullptr
        && pressedControl_->type == SkinControl::Type::Pad)
        releasePad(pressedControl_->index);

    draggingKnob_   = nullptr;
    pressedControl_ = nullptr;
    repaint();
}

void PanelComponent::mouseMove(const MouseEvent& event) {
    if (!skin_.isValid()) return;

    const auto* control = skin_.hitTest(toNormalised(event.getPosition()));
    if (control == hoveredControl_) return;

    hoveredControl_ = control;

    // Hovering writes into the context bar rather than popping a tooltip. It
    // reuses the line that already exists, appears instantly, and does not
    // cover the panel - and on a panel whose only native readout is three
    // digits, "what is this control" needs to be answerable at a glance.
    if (control != nullptr && control->label.isNotEmpty()) {
        hoverText_ = control->label;
        if (control->type == SkinControl::Type::Knob)
            hoverText_ += "  -  drag vertically, shift for fine";
    } else {
        hoverText_.clear();
    }

    repaint();
}

void PanelComponent::mouseExit(const MouseEvent&) {
    hoveredControl_ = nullptr;
    hoverText_.clear();
    repaint();
}

// --- computer keyboard -----------------------------------------------------

int PanelComponent::keyToPadIndex(int keyCode) noexcept {
    for (int i = 0; i < sp303::kNumPads; ++i)
        if (kPadKeys[i] == keyCode) return i;
    return -1;
}

bool PanelComponent::keyPressed(const KeyPress& key) {
    const int pad = keyToPadIndex(key.getKeyCode());
    if (pad < 0) return false;

    // Ignore auto-repeat: holding a key must sustain, not machine-gun.
    if ((keyboardPads_ & (1u << pad)) != 0) return true;

    keyboardPads_ |= static_cast<std::uint8_t>(1u << pad);
    triggerPad(pad);
    repaint();
    return true;
}

bool PanelComponent::keyStateChanged(bool /*isKeyDown*/) {
    // JUCE has no key-up callback, so poll the keys we believe are held and
    // release the ones that are not.
    for (int i = 0; i < sp303::kNumPads; ++i) {
        const bool wasHeld = (keyboardPads_ & (1u << i)) != 0;
        if (wasHeld && !KeyPress::isKeyCurrentlyDown(kPadKeys[i])) {
            keyboardPads_ &= static_cast<std::uint8_t>(~(1u << i));
            releasePad(i);
            repaint();
        }
    }
    return false;
}

// --- drag and drop ---------------------------------------------------------

bool PanelComponent::isInterestedInFileDrag(const StringArray& files) {
    for (const auto& path : files) {
        const auto extension = File(path).getFileExtension().toLowerCase();
        if (extension == ".wav" || extension == ".aif" || extension == ".aiff"
            || extension == ".flac" || extension == ".mp3" || extension == ".ogg")
            return true;
    }
    return false;
}

void PanelComponent::fileDragEnter(const StringArray& files, int x, int y) {
    fileDragMove(files, x, y);
}

void PanelComponent::fileDragMove(const StringArray&, int x, int y) {
    const auto* control = skin_.hitTest(toNormalised({x, y}));
    const int pad = (control != nullptr && control->type == SkinControl::Type::Pad)
                        ? control->index : -1;

    if (pad == dropTargetPad_) return;
    dropTargetPad_ = pad;
    repaint();
}

void PanelComponent::fileDragExit(const StringArray&) {
    dropTargetPad_ = -1;
    repaint();
}

void PanelComponent::filesDropped(const StringArray& files, int x, int y) {
    dropTargetPad_ = -1;

    const auto* control = skin_.hitTest(toNormalised({x, y}));
    if (control == nullptr || control->type != SkinControl::Type::Pad
        || files.isEmpty()) {
        repaint();
        return;
    }

    const int slot = processor_.currentBankIndex() * sp303::kNumPads + control->index;
    processor_.loadSampleIntoSlot(slot, File(files[0]));

    statusText_ = "Loaded " + File(files[0]).getFileName()
                + " -> pad " + String(control->index + 1);
    repaint();
}

// --- interaction helpers ---------------------------------------------------

void PanelComponent::triggerPad(int padIndex) {
    if (padIndex < 0 || padIndex >= sp303::kNumPads) return;

    // While RESAMPLE is waiting for a destination the pads are a chooser, not
    // an instrument. Playing the pad as well would print the very sound the
    // user is about to record over.
    if (awaitingResampleTarget_) {
        chooseResampleDestination(padIndex);
        consumedPads_ |= static_cast<std::uint8_t>(1u << padIndex);
        return;
    }

    sp303::Command command;
    command.type     = sp303::Command::Type::NoteOn;
    command.intValue = padIndex;
    processor_.device().commandQueue().push(command);
}

void PanelComponent::releasePad(int padIndex) {
    if (padIndex < 0 || padIndex >= sp303::kNumPads) return;

    // The press was swallowed by destination selection, so there is no note to
    // release. Sending one anyway would cut a pad that was already sounding.
    if ((consumedPads_ & (1u << padIndex)) != 0) {
        consumedPads_ &= static_cast<std::uint8_t>(~(1u << padIndex));
        return;
    }

    sp303::Command command;
    command.type     = sp303::Command::Type::NoteOff;
    command.intValue = padIndex;
    processor_.device().commandQueue().push(command);
}

void PanelComponent::performAction(const SkinControl& control) {
    const auto& action = control.action;

    if (action == "selectEffect") {
        processor_.setEffectIndex(control.value);
    } else if (action == "selectBank") {
        processor_.setBankIndex(control.value);
    } else if (action.startsWith("setQuality:")) {
        processor_.setQualityIndex(action.fromFirstOccurrenceOf(":", false, false)
                                       .getIntValue());
    } else if (action == "resample") {
        beginResampleTargeting();
    } else if (action == "rec") {
        toggleRecording();
    } else if (action == "cancel") {
        abortResample();
    } else {
        // TODO(phase-2/3): delete, mark, the edit buttons and the playback
        // toggles. Deliberately inert rather than half-wired - a button that
        // does something unpredictable is worse than one that visibly does
        // nothing yet.
        statusText_ = control.label + " - not implemented yet";
    }
}

// --- resample ---------------------------------------------------------------
//
// The hardware's ordering, kept: RESAMPLE picks where the take will land, REC
// starts and stops it, CANCEL abandons it. Choosing the destination first is
// what makes it impossible to finish a capture with nowhere to put it.

bool PanelComponent::resampleInProgress() const noexcept {
    return resampleState_ == sp303::ResampleRecorder::State::Recording;
}

sp303::ResampleRecorder::State PanelComponent::refreshResampleState() noexcept {
    resampleState_ = processor_.device().resampler().state();
    return resampleState_;
}

void PanelComponent::beginResampleTargeting() {
    if (refreshResampleState() == sp303::ResampleRecorder::State::Recording) {
        // RESAMPLE doubles as a stop while a take is running, so the gesture
        // that started it also ends it without reaching for another button.
        processor_.stopResample();
        statusText_ = "RESAMPLE stopped";
        return;
    }

    if (awaitingResampleTarget_) {
        awaitingResampleTarget_ = false;
        statusText_ = "RESAMPLE cancelled";
        return;
    }

    awaitingResampleTarget_ = true;
    statusText_ = "RESAMPLE - hit a pad to choose the destination";
}

void PanelComponent::chooseResampleDestination(int padIndex) {
    awaitingResampleTarget_ = false;

    // Bank-relative, like every other pad operation in the panel: the pad the
    // user pressed is the pad they meant, in the bank they are looking at.
    const int slot = processor_.currentBankIndex() * sp303::kNumPads + padIndex;
    processor_.armResample(slot);
    refreshResampleState();

    statusText_ = "RESAMPLE armed -> pad " + String(padIndex + 1)
                + " - press REC to start";
}

void PanelComponent::toggleRecording() {
    switch (refreshResampleState()) {
        case sp303::ResampleRecorder::State::Armed:
            processor_.startResample();
            statusText_ = "REC - capturing the output, press REC again to stop";
            break;

        case sp303::ResampleRecorder::State::Recording:
            processor_.stopResample();
            statusText_ = "REC stopped";
            break;

        case sp303::ResampleRecorder::State::Finished:
            // The processor collects finished takes on its own 20 Hz timer.
            statusText_ = "REC - still writing the last take to its pad";
            break;

        case sp303::ResampleRecorder::State::Idle:
        default:
            // REC has no meaning on its own yet. Sampling from the plugin's
            // audio input is a separate feature (see docs/ARCHITECTURE.md,
            // "Sampling into the plugin"); until it exists, saying so beats
            // silently doing nothing.
            statusText_ = awaitingResampleTarget_
                ? "REC - choose a destination pad first"
                : "REC - press RESAMPLE first (input sampling not implemented)";
            break;
    }
}

void PanelComponent::abortResample() {
    if (awaitingResampleTarget_
        || refreshResampleState() != sp303::ResampleRecorder::State::Idle) {
        awaitingResampleTarget_ = false;
        processor_.cancelResample();
        statusText_ = "Resample cancelled";
        return;
    }

    // With nothing to cancel, CANCEL is a panic button - which is what it is
    // for on the hardware.
    sp303::Command command;
    command.type = sp303::Command::Type::AllNotesOff;
    processor_.device().commandQueue().push(command);
    statusText_ = "All pads stopped";
}

float PanelComponent::parameterValue(const String& paramId) const {
    if (paramId.isEmpty()) return 0.0f;
    if (auto* raw = processor_.parameters().getRawParameterValue(paramId))
        return raw->load();
    return 0.0f;
}

void PanelComponent::setParameterValue(const String& paramId, float value) {
    if (paramId.isEmpty()) return;

    // Go through the host-visible parameter so the change is recorded for
    // automation and undo, rather than poking the engine directly.
    if (auto* parameter = processor_.parameters().getParameter(paramId)) {
        parameter->setValueNotifyingHost(value);
    }
}

// --- telemetry -------------------------------------------------------------

void PanelComponent::timerCallback() {
    sp303::Telemetry latest {};
    bool received = false;

    // Drain fully and keep only the newest: older meter frames are stale by
    // definition.
    while (processor_.device().telemetryQueue().pop(latest))
        received = true;

    if (received) {
        padStates_    = latest.padStates;
        activeVoices_ = latest.activeVoices;
        peakL_        = latest.outputPeakL;
        peakR_        = latest.outputPeakR;
        displayText_  = String(activeVoices_).paddedLeft('0', 3);
    }

    // The recorder's state is level-triggered on purpose (see
    // ResampleRecorder.h), so polling it is the intended way to read it - a
    // missed message cannot lose a take.
    auto& recorder = processor_.device().resampler();
    resampleState_ = recorder.state();
    resampleFill_  = recorder.fillFraction();

    // A capture that finishes while the panel is doing nothing else still has
    // to say so. The processor publishes the message; the panel shows it once.
    const auto processorStatus = processor_.resampleStatus();
    if (processorStatus != lastProcessorStatus_) {
        lastProcessorStatus_ = processorStatus;
        if (processorStatus.isNotEmpty()) statusText_ = processorStatus;
    }

    // The 3-digit display is the only native readout, so a running capture
    // takes it over: filling the buffer is the one thing that ends a take
    // without the user asking, and being surprised by it is the failure mode.
    if (awaitingResampleTarget_)
        displayText_ = "rSP";
    else if (resampleInProgress())
        displayText_ = String(juce::roundToInt(resampleFill_ * 100.0f))
                           .paddedLeft('0', 3);

    auto& rack = processor_.device().effectRack();
    knobNamesText_ = String(rack.ctrl1Name()) + "  /  "
                   + String(rack.ctrl2Name()) + "  /  "
                   + String(rack.ctrl3Name());

    repaint();
}
