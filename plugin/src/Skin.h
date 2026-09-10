#pragma once

#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

// ---------------------------------------------------------------------------
// A panel skin: one background image plus a map of where every control sits.
//
// The artwork is DATA, not code. Swapping the default vector panel for a
// photograph of real hardware is a matter of dropping in a file and
// re-measuring the hitboxes - no C++ changes. That separation is what keeps
// the art decision from blocking the engineering, and it is why the layout and
// the hitbox map are generated together from one source
// (tools/skin/build_skin.py).
//
// Coordinates are stored NORMALISED (0..1) relative to the source image. The
// panel can then be drawn at any size and the hitboxes scale with it, which is
// what makes the GUI resizable without a second coordinate system.
// ---------------------------------------------------------------------------

struct SkinControl {
    enum class Type { Pad, Knob, Button, Toggle, Display, Label, Meter, Unknown };

    Type                   type {Type::Unknown};
    juce::String           id;
    juce::Rectangle<float> bounds;      // normalised 0..1
    int                    index {-1};  // pad index, 0-7
    juce::String           param;       // APVTS parameter id, for knobs
    juce::String           action;      // command name, for buttons
    int                    value {0};   // action payload
    juce::String           label;

    bool isInteractive() const noexcept {
        return type == Type::Pad || type == Type::Knob
            || type == Type::Button || type == Type::Toggle;
    }

    static Type typeFromString(const juce::String& s) noexcept;
};

class Skin {
public:
    // Reads panel.json from `skinDirectory` and loads the artwork it names.
    // Returns false and sets errorMessage() on failure - callers should fall
    // back to a drawn placeholder rather than showing an empty window.
    bool load(const juce::File& skinDirectory);

    bool isValid() const noexcept { return valid_; }
    const juce::String& errorMessage() const noexcept { return error_; }

    const std::vector<SkinControl>& controls() const noexcept { return controls_; }

    // Source-image aspect ratio, used to letterbox the panel when the window
    // does not match it.
    float aspectRatio() const noexcept { return aspect_; }

    void drawBackground(juce::Graphics& g, juce::Rectangle<float> area) const;

    // `point` is normalised. Returns nullptr when nothing interactive is hit.
    const SkinControl* hitTest(juce::Point<float> point) const noexcept;

    const SkinControl* findById(const juce::String& id) const noexcept;
    const SkinControl* findPad(int padIndex) const noexcept;

private:
    std::vector<SkinControl>        controls_;
    std::unique_ptr<juce::Drawable> vectorArt_;   // SVG
    juce::Image                     rasterArt_;   // PNG / JPEG
    float                           aspect_ {1240.0f / 720.0f};
    bool                            valid_ {false};
    juce::String                    error_;
};
