#include "Skin.h"

using namespace juce;

SkinControl::Type SkinControl::typeFromString(const String& s) noexcept {
    if (s == "pad")     return Type::Pad;
    if (s == "knob")    return Type::Knob;
    if (s == "button")  return Type::Button;
    if (s == "toggle")  return Type::Toggle;
    if (s == "display") return Type::Display;
    if (s == "label")   return Type::Label;
    if (s == "meter")   return Type::Meter;
    return Type::Unknown;
}

namespace {

// Small helper: JUCE's var property access is verbose enough that inlining it
// at every call site obscures the parsing logic.
var property(const var& object, const char* key) {
    return object.getProperty(Identifier(key), var());
}

}  // namespace

bool Skin::load(const File& skinDirectory) {
    valid_ = false;
    error_.clear();
    controls_.clear();
    vectorArt_.reset();
    rasterArt_ = {};

    const auto manifestFile = skinDirectory.getChildFile("panel.json");
    if (!manifestFile.existsAsFile()) {
        error_ = "panel.json not found in " + skinDirectory.getFullPathName();
        return false;
    }

    const auto manifest = JSON::parse(manifestFile.loadFileAsString());
    if (!manifest.isObject()) {
        error_ = "panel.json is not a JSON object";
        return false;
    }

    const auto sourceWidth  = static_cast<float>(property(manifest, "sourceWidth"));
    const auto sourceHeight = static_cast<float>(property(manifest, "sourceHeight"));

    if (sourceWidth <= 0.0f || sourceHeight <= 0.0f) {
        error_ = "sourceWidth/sourceHeight missing or invalid";
        return false;
    }
    aspect_ = sourceWidth / sourceHeight;

    // --- artwork ------------------------------------------------------------
    const auto imageName = property(manifest, "image").toString();
    const auto imageFile = skinDirectory.getChildFile(imageName);

    if (!imageFile.existsAsFile()) {
        error_ = "artwork '" + imageName + "' not found";
        return false;
    }

    if (imageFile.hasFileExtension("svg")) {
        if (auto xml = XmlDocument::parse(imageFile))
            vectorArt_ = Drawable::createFromSVG(*xml);

        if (vectorArt_ == nullptr) {
            error_ = "failed to parse SVG '" + imageName + "'";
            return false;
        }
    } else {
        rasterArt_ = ImageFileFormat::loadFrom(imageFile);
        if (!rasterArt_.isValid()) {
            error_ = "failed to decode image '" + imageName + "'";
            return false;
        }
    }

    // --- controls -----------------------------------------------------------
    const auto controlsVar = property(manifest, "controls");
    auto* array = controlsVar.getArray();
    if (array == nullptr) {
        error_ = "'controls' is missing or not an array";
        return false;
    }

    controls_.reserve(static_cast<std::size_t>(array->size()));

    for (const auto& entry : *array) {
        SkinControl control;
        control.id     = property(entry, "id").toString();
        control.type   = SkinControl::typeFromString(property(entry, "type").toString());
        control.param  = property(entry, "param").toString();
        control.action = property(entry, "action").toString();
        control.label  = property(entry, "label").toString();

        const auto indexVar = property(entry, "index");
        control.index = indexVar.isVoid() ? -1 : static_cast<int>(indexVar);
        control.value = static_cast<int>(property(entry, "value"));

        auto* rect = property(entry, "rect").getArray();
        if (rect == nullptr || rect->size() != 4) {
            error_ = "control '" + control.id + "' has no valid rect";
            return false;
        }

        // Normalise here, once, so nothing downstream has to know the source
        // image dimensions.
        control.bounds = Rectangle<float>(
            static_cast<float>((*rect)[0]) / sourceWidth,
            static_cast<float>((*rect)[1]) / sourceHeight,
            static_cast<float>((*rect)[2]) / sourceWidth,
            static_cast<float>((*rect)[3]) / sourceHeight);

        controls_.push_back(std::move(control));
    }

    if (controls_.empty()) {
        error_ = "skin defines no controls";
        return false;
    }

    valid_ = true;
    return true;
}

void Skin::drawBackground(Graphics& g, Rectangle<float> area) const {
    if (vectorArt_ != nullptr) {
        // Stretch to fill: the caller has already letterboxed `area` to the
        // skin's aspect ratio, so this does not distort.
        vectorArt_->drawWithin(g, area, RectanglePlacement::stretchToFit, 1.0f);
    } else if (rasterArt_.isValid()) {
        g.drawImage(rasterArt_, area, RectanglePlacement::stretchToFit);
    }
}

const SkinControl* Skin::hitTest(Point<float> point) const noexcept {
    // Reverse order so later entries sit "on top", matching paint order.
    for (auto it = controls_.rbegin(); it != controls_.rend(); ++it)
        if (it->isInteractive() && it->bounds.contains(point))
            return &(*it);
    return nullptr;
}

const SkinControl* Skin::findById(const String& id) const noexcept {
    for (const auto& control : controls_)
        if (control.id == id) return &control;
    return nullptr;
}

const SkinControl* Skin::findPad(int padIndex) const noexcept {
    for (const auto& control : controls_)
        if (control.type == SkinControl::Type::Pad && control.index == padIndex)
            return &control;
    return nullptr;
}
