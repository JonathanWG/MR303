#include "PluginProcessor.h"

#include "PluginEditor.h"

using namespace juce;

// ---------------------------------------------------------------------------
// Parameters
//
// CTRL 1/2/3 are exposed to the host even in Authentic mode. The hardware has
// no automation, but withholding it would cost users something real and buy
// no sonic fidelity - the audio path is identical either way.
// ---------------------------------------------------------------------------
AudioProcessorValueTreeState::ParameterLayout
Sp303AudioProcessor::createParameterLayout() {
    std::vector<std::unique_ptr<RangedAudioParameter>> params;

    params.push_back(std::make_unique<AudioParameterFloat>(
        ParameterID {"ctrl1", 1}, "CTRL 1", NormalisableRange<float>(0.0f, 1.0f), 0.5f));
    params.push_back(std::make_unique<AudioParameterFloat>(
        ParameterID {"ctrl2", 1}, "CTRL 2", NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    params.push_back(std::make_unique<AudioParameterFloat>(
        ParameterID {"ctrl3", 1}, "CTRL 3", NormalisableRange<float>(0.0f, 1.0f), 0.0f));

    params.push_back(std::make_unique<AudioParameterChoice>(
        ParameterID {"effect", 1}, "Effect",
        StringArray {"None", "Filter+Drive", "Pitch", "Delay", "Vinyl Sim", "Isolator"}, 0));

    params.push_back(std::make_unique<AudioParameterChoice>(
        ParameterID {"quality", 1}, "Quality",
        StringArray {"Standard", "Long", "Lo-Fi"}, 0));

    params.push_back(std::make_unique<AudioParameterChoice>(
        ParameterID {"bank", 1}, "Bank", StringArray {"A", "B", "C", "D"}, 0));

    params.push_back(std::make_unique<AudioParameterChoice>(
        ParameterID {"fidelity", 1}, "Fidelity",
        StringArray {"Authentic", "Modern"}, 0));

    params.push_back(std::make_unique<AudioParameterFloat>(
        ParameterID {"volume", 1}, "Volume",
        NormalisableRange<float>(0.0f, 1.0f), 0.8f));

    return {params.begin(), params.end()};
}

// ---------------------------------------------------------------------------
// Panel accessors
// ---------------------------------------------------------------------------
namespace {

int choiceIndex(AudioProcessorValueTreeState& state, const char* id) {
    if (auto* raw = state.getRawParameterValue(id))
        return static_cast<int>(raw->load());
    return 0;
}

void setChoiceIndex(AudioProcessorValueTreeState& state, const char* id, int index) {
    auto* parameter = state.getParameter(id);
    if (parameter == nullptr) return;

    // Choice parameters are normalised 0..1 across their steps, so the raw
    // index has to be converted before it is sent to the host.
    parameter->setValueNotifyingHost(
        parameter->convertTo0to1(static_cast<float>(index)));
}

}  // namespace

int Sp303AudioProcessor::currentEffectIndex() const noexcept {
    return choiceIndex(const_cast<AudioProcessorValueTreeState&>(params_), "effect");
}

int Sp303AudioProcessor::currentBankIndex() const noexcept {
    return choiceIndex(const_cast<AudioProcessorValueTreeState&>(params_), "bank");
}

int Sp303AudioProcessor::currentQualityIndex() const noexcept {
    return choiceIndex(const_cast<AudioProcessorValueTreeState&>(params_), "quality");
}

void Sp303AudioProcessor::setEffectIndex(int index) {
    setChoiceIndex(params_, "effect", index);
}

void Sp303AudioProcessor::setBankIndex(int index) {
    setChoiceIndex(params_, "bank", index);

    sp303::Command command;
    command.type     = sp303::Command::Type::SelectBank;
    command.intValue = index;
    device_.commandQueue().push(command);
}

void Sp303AudioProcessor::setQualityIndex(int index) {
    setChoiceIndex(params_, "quality", index);

    // NOT sent through the command queue: setQualityMode() reallocates the
    // decimator's FIR state, which must not happen on the audio thread.
    // Suspending processing is the cheap, correct way to do it.
    suspendProcessing(true);
    device_.setQualityMode(static_cast<sp303::QualityMode>(index));
    suspendProcessing(false);
}

Sp303AudioProcessor::Sp303AudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withOutput("Output", AudioChannelSet::stereo(), true)
                         .withInput("Input", AudioChannelSet::stereo(), true)),
      params_(*this, nullptr, "SP303", createParameterLayout()) {
    formatManager_.registerBasicFormats();

    // 20 Hz is plenty to notice a finished capture without burning cycles.
    startTimerHz(20);
}

Sp303AudioProcessor::~Sp303AudioProcessor() {
    stopTimer();
}

void Sp303AudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    device_.prepare(sampleRate, samplesPerBlock);
}

void Sp303AudioProcessor::releaseResources() {
    device_.reset();
}

bool Sp303AudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    return layouts.getMainOutputChannelSet() == AudioChannelSet::stereo();
}

sp303::seq::TransportInfo Sp303AudioProcessor::readTransport() {
    sp303::seq::TransportInfo info;

    if (auto* playHead = getPlayHead()) {
        if (auto position = playHead->getPosition()) {
            info.bpm       = position->getBpm().orFallback(120.0);
            info.isPlaying = position->getIsPlaying();
            info.positionInQuarters = position->getPpqPosition().orFallback(0.0);

            // Detect scrubs and loop wraps. Without this the sequencer would
            // integrate a jump as elapsed musical time and fire a burst of
            // stale events.
            if (lastKnownPpq_ >= 0.0) {
                const auto delta = info.positionInQuarters - lastKnownPpq_;
                info.didJump = (delta < 0.0) || (delta > 1.0);
            }
            lastKnownPpq_ = info.positionInQuarters;
        }
    }
    return info;
}

void Sp303AudioProcessor::handleMidi(const MidiBuffer& midi) {
    auto& queue = device_.commandQueue();

    for (const auto metadata : midi) {
        const auto message = metadata.getMessage();

        if (message.isNoteOn()) {
            const int pad = message.getNoteNumber() - padBaseNote_;
            if (pad >= 0 && pad < sp303::kNumPads) {
                sp303::Command cmd;
                cmd.type     = sp303::Command::Type::NoteOn;
                cmd.intValue = pad;
                // TODO(phase-2): honour velocity in Modern mode. The hardware
                // pads are not velocity sensitive, so Authentic ignores it.
                cmd.floatValue = message.getFloatVelocity();
                queue.push(cmd);
            }
        } else if (message.isNoteOff()) {
            const int pad = message.getNoteNumber() - padBaseNote_;
            if (pad >= 0 && pad < sp303::kNumPads) {
                sp303::Command cmd;
                cmd.type     = sp303::Command::Type::NoteOff;
                cmd.intValue = pad;
                queue.push(cmd);
            }
        } else if (message.isAllNotesOff() || message.isAllSoundOff()) {
            sp303::Command cmd;
            cmd.type = sp303::Command::Type::AllNotesOff;
            queue.push(cmd);
        }
    }
}

void Sp303AudioProcessor::processBlock(AudioBuffer<float>& buffer, MidiBuffer& midi) {
    ScopedNoDenormals noDenormals;

    handleMidi(midi);

    // Parameters are read as plain atomics and smoothed inside the core.
    device_.ctrl1.store(params_.getRawParameterValue("ctrl1")->load(),
                        std::memory_order_relaxed);
    device_.ctrl2.store(params_.getRawParameterValue("ctrl2")->load(),
                        std::memory_order_relaxed);
    device_.ctrl3.store(params_.getRawParameterValue("ctrl3")->load(),
                        std::memory_order_relaxed);

    const auto effectIndex =
        static_cast<int>(params_.getRawParameterValue("effect")->load());
    {
        sp303::Command cmd;
        cmd.type     = sp303::Command::Type::SelectEffect;
        cmd.intValue = effectIndex;
        device_.commandQueue().push(cmd);
    }

    auto* left  = buffer.getWritePointer(0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : left;

    device_.processBlock(left, right, buffer.getNumSamples(), readTransport());

    // Mono host: mirror the left channel rather than leaving stale data.
    if (buffer.getNumChannels() == 1)
        buffer.copyFrom(0, 0, left, buffer.getNumSamples());
}

void Sp303AudioProcessor::loadSampleIntoSlot(int slotIndex, const File& file) {
    // TODO(phase-2): move decoding to a background thread. Doing it inline on
    // the message thread stalls the UI on large files, and streaming from disk
    // for long samples is a separate design item.
    std::unique_ptr<AudioFormatReader> reader(formatManager_.createReaderFor(file));
    if (reader == nullptr) return;

    const auto numChannels = static_cast<int>(reader->numChannels);
    const auto numFrames   = static_cast<int>(reader->lengthInSamples);
    if (numFrames <= 0) return;

    AudioBuffer<float> temp(numChannels, numFrames);
    reader->read(&temp, 0, numFrames, 0, true, true);

    std::vector<std::vector<float>> channels(static_cast<std::size_t>(numChannels));
    for (int ch = 0; ch < numChannels; ++ch)
        channels[static_cast<std::size_t>(ch)].assign(
            temp.getReadPointer(ch), temp.getReadPointer(ch) + numFrames);

    auto sample = std::make_shared<const sp303::SampleBuffer>(
        std::move(channels), reader->sampleRate, file.getFileNameWithoutExtension().toStdString());

    // Atomic swap - the audio thread picks this up without blocking.
    device_.setSlotSample(slotIndex, std::move(sample));

    auto& pad = device_.pad(slotIndex / sp303::kNumPads, slotIndex % sp303::kNumPads);
    pad.slotIndex = slotIndex;
    pad.startFrame = 0;
    pad.endFrame   = 0;  // 0 = play to the end
}

// ---------------------------------------------------------------------------
// Resample
// ---------------------------------------------------------------------------
void Sp303AudioProcessor::armResample(int destinationSlot) {
    device_.armResample(destinationSlot);

    const ScopedLock lock(statusLock_);
    resampleStatus_ = "RESAMPLE armed -> slot " + String(destinationSlot + 1)
                    + " - press REC";
}

void Sp303AudioProcessor::startResample() {
    sp303::Command command;
    command.type = sp303::Command::Type::StartResample;
    device_.commandQueue().push(command);
}

void Sp303AudioProcessor::stopResample() {
    sp303::Command command;
    command.type = sp303::Command::Type::StopResample;
    device_.commandQueue().push(command);
}

void Sp303AudioProcessor::cancelResample() {
    device_.resampler().cancel();

    const ScopedLock lock(statusLock_);
    resampleStatus_.clear();
}

String Sp303AudioProcessor::resampleStatus() const {
    const ScopedLock lock(statusLock_);
    return resampleStatus_;
}

void Sp303AudioProcessor::timerCallback() {
    collectFinishedResample();

    // Frees sample buffers displaced by a load or a resample. This is the only
    // place they are released - the audio thread must never run a destructor.
    // See sp303::Device::collectRetiredSamples().
    device_.collectRetiredSamples();
}

void Sp303AudioProcessor::collectFinishedResample() {
    auto& recorder = device_.resampler();

    if (recorder.state() != sp303::ResampleRecorder::State::Finished)
        return;

    const int slot   = recorder.destinationSlot();
    const int frames = recorder.capturedFrames();

    if (slot < 0 || frames <= 0) {
        recorder.reset();
        return;
    }

    // Copy out of the recorder's fixed buffer into a right-sized immutable
    // SampleBuffer. Done here on the message thread; the audio thread only
    // ever wrote into memory that already existed.
    std::vector<std::vector<float>> channels(2);
    for (int ch = 0; ch < 2; ++ch) {
        const float* source = recorder.channel(ch);
        channels[static_cast<std::size_t>(ch)].assign(source, source + frames);
    }

    auto sample = std::make_shared<const sp303::SampleBuffer>(
        std::move(channels), recorder.sampleRate(),
        "resample " + std::to_string(slot + 1));

    device_.setSlotSample(slot, sample);

    auto& pad = device_.pad(slot / sp303::kNumPads, slot % sp303::kNumPads);
    pad.slotIndex  = slot;
    pad.startFrame = 0;
    pad.endFrame   = 0;

    recorder.reset();

    {
        const ScopedLock lock(statusLock_);
        resampleStatus_ = "Resampled " + String(frames / recorder.sampleRate(), 2)
                        + "s -> pad " + String(slot % sp303::kNumPads + 1);
    }
}

// ---------------------------------------------------------------------------
// State
//
// Pad audio is EMBEDDED in the project, not referenced by path. It makes
// project files large, and it is still the right default: a session that
// silently loses its samples because a folder moved is the single most
// common complaint about sampler plugins.
// ---------------------------------------------------------------------------
namespace {
constexpr int kStateVersion = 1;
const Identifier kRoot   {"SP303_STATE"};
const Identifier kPads   {"PADS"};
const Identifier kPad    {"PAD"};
}  // namespace

bool Sp303AudioProcessor::encodeSample(const sp303::SampleBuffer& sample,
                                       MemoryBlock& destination) {
    if (sample.isEmpty()) return false;

    const int numChannels = sample.numChannels();
    const int numFrames   = sample.numFrames();

    AudioBuffer<float> temp(numChannels, numFrames);
    for (int ch = 0; ch < numChannels; ++ch)
        temp.copyFrom(ch, 0, sample.channel(ch), numFrames);

    FlacAudioFormat format;
    auto stream = std::make_unique<MemoryOutputStream>(destination, false);

    // 24-bit: transparent for anything this instrument will ever hold, and
    // roughly a third smaller than 32-bit float would be.
    std::unique_ptr<AudioFormatWriter> writer(
        format.createWriterFor(stream.get(), sample.sourceRate(),
                               static_cast<unsigned int>(numChannels),
                               24, {}, 0));

    if (writer == nullptr) return false;
    stream.release();  // the writer owns it now

    return writer->writeFromAudioSampleBuffer(temp, 0, numFrames);
}

sp303::SampleBufferPtr Sp303AudioProcessor::decodeSample(
    const MemoryBlock& encoded, double sampleRate, const String& name) {

    if (encoded.getSize() == 0) return {};

    FlacAudioFormat format;
    std::unique_ptr<AudioFormatReader> reader(format.createReaderFor(
        new MemoryInputStream(encoded, false), true));

    if (reader == nullptr) return {};

    const auto numChannels = static_cast<int>(reader->numChannels);
    const auto numFrames   = static_cast<int>(reader->lengthInSamples);
    if (numFrames <= 0 || numChannels <= 0) return {};

    AudioBuffer<float> temp(numChannels, numFrames);
    reader->read(&temp, 0, numFrames, 0, true, true);

    std::vector<std::vector<float>> channels(static_cast<std::size_t>(numChannels));
    for (int ch = 0; ch < numChannels; ++ch)
        channels[static_cast<std::size_t>(ch)].assign(
            temp.getReadPointer(ch), temp.getReadPointer(ch) + numFrames);

    // Prefer the rate stored alongside the blob: FLAC rounds to integer Hz.
    const double rate = sampleRate > 0.0 ? sampleRate : reader->sampleRate;

    return std::make_shared<const sp303::SampleBuffer>(
        std::move(channels), rate, name.toStdString());
}

void Sp303AudioProcessor::getStateInformation(MemoryBlock& destData) {
    ValueTree root(kRoot);
    root.setProperty("version", kStateVersion, nullptr);
    root.appendChild(params_.copyState(), nullptr);

    ValueTree pads(kPads);

    for (int slot = 0; slot < sp303::kNumSlots; ++slot) {
        const auto& pad = device_.pad(slot / sp303::kNumPads,
                                      slot % sp303::kNumPads);

        ValueTree entry(kPad);
        entry.setProperty("slot",     slot, nullptr);
        entry.setProperty("start",    static_cast<int>(pad.startFrame), nullptr);
        entry.setProperty("end",      static_cast<int>(pad.endFrame), nullptr);
        entry.setProperty("level",    pad.level, nullptr);
        entry.setProperty("trigger",  static_cast<int>(pad.trigger), nullptr);
        entry.setProperty("loop",     static_cast<int>(pad.loop), nullptr);
        entry.setProperty("reverse",  pad.reverse, nullptr);
        entry.setProperty("hold",     pad.hold, nullptr);
        entry.setProperty("speed",    pad.speedRatio, nullptr);

        if (auto sample = device_.slotSample(slot); sample && !sample->isEmpty()) {
            MemoryBlock encoded;
            if (encodeSample(*sample, encoded)) {
                entry.setProperty("audio", var(encoded), nullptr);
                entry.setProperty("rate",  sample->sourceRate(), nullptr);
                entry.setProperty("name",  String(sample->name()), nullptr);
            }
        }

        pads.appendChild(entry, nullptr);
    }

    root.appendChild(pads, nullptr);

    // Binary stream rather than XML: XML would base64 every audio blob and
    // inflate the project by a third for no benefit.
    MemoryOutputStream stream(destData, false);
    root.writeToStream(stream);
}

void Sp303AudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    MemoryInputStream stream(data, static_cast<std::size_t>(sizeInBytes), false);
    auto root = ValueTree::readFromStream(stream);

    if (!root.hasType(kRoot)) {
        // Older builds wrote bare XML parameter state. Read what we can rather
        // than discarding the user's session.
        if (auto xml = getXmlFromBinary(data, sizeInBytes))
            params_.replaceState(ValueTree::fromXml(*xml));
        return;
    }

    if (auto parameters = root.getChildWithName(params_.state.getType());
        parameters.isValid())
        params_.replaceState(parameters);

    auto pads = root.getChildWithName(kPads);
    if (!pads.isValid()) return;

    for (const auto& entry : pads) {
        const int slot = entry.getProperty("slot", -1);
        if (slot < 0 || slot >= sp303::kNumSlots) continue;

        auto& pad = device_.pad(slot / sp303::kNumPads, slot % sp303::kNumPads);
        pad.startFrame = static_cast<std::uint32_t>(
            static_cast<int>(entry.getProperty("start", 0)));
        pad.endFrame = static_cast<std::uint32_t>(
            static_cast<int>(entry.getProperty("end", 0)));
        pad.level      = entry.getProperty("level", 1.0f);
        pad.trigger    = static_cast<sp303::TriggerMode>(
            static_cast<int>(entry.getProperty("trigger", 0)));
        pad.loop       = static_cast<sp303::LoopMode>(
            static_cast<int>(entry.getProperty("loop", 0)));
        pad.reverse    = entry.getProperty("reverse", false);
        pad.hold       = entry.getProperty("hold", false);
        pad.speedRatio = entry.getProperty("speed", 1.0f);

        if (const auto* audio = entry.getProperty("audio").getBinaryData()) {
            const double rate = entry.getProperty("rate", 44100.0);
            const String name = entry.getProperty("name", "");

            if (auto sample = decodeSample(*audio, rate, name)) {
                device_.setSlotSample(slot, std::move(sample));
                pad.slotIndex = slot;
                continue;
            }
        }

        // No audio for this slot: make sure the pad does not point at a slot
        // that is now empty, or it would try to play nothing.
        pad.slotIndex = -1;
    }
}

AudioProcessorEditor* Sp303AudioProcessor::createEditor() {
    return new Sp303AudioProcessorEditor(*this);
}

// ---------------------------------------------------------------------------
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new Sp303AudioProcessor();
}
