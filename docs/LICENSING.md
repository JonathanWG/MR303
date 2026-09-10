# Licensing and legal — blocking items

These are not paperwork to sort out before release. Several of them constrain
code and design decisions, so they need answers **now**.

---

## 1. Do not touch Roland firmware

**Allowed:** black-box behavioural measurement — feed signals in, record what
comes out, infer the behaviour, implement it independently. That is what
`docs/PHASE0.md` describes.

**Not allowed:** dumping ROM, disassembling firmware, porting or transcribing
Roland's code in any form.

The distinction is the whole legal foundation of the project. Keep the Phase 0
capture logs — documented clean-room provenance is what makes the work
defensible if anyone asks.

## 2. Trademarks

"Roland", "Boss", "Dr. Sample" and "SP-303" are Roland's marks.

- The product cannot be **named** using them.
- They cannot be the identity of the marketing.
- Factual comparative reference is generally acceptable, but is not the same as
  branding.

The internal repo name `sp303` and the placeholder `PRODUCT_NAME "SP303"` in
`plugin/CMakeLists.txt` are **development placeholders** and must be changed
before anything ships. Hardware manufacturers have historically acted against
emulations that use their marks.

## 3. Trade dress

Copying the panel's visual identity — colours, layout art, typography, overall
look — carries real risk even with a different name.

The approach in `docs/ARCHITECTURE.md` is to replicate the **functional
topology** (8 pads, 3 CTRL knobs, dedicated effect buttons, 3-digit display)
with an original visual identity.

This costs nothing in usability. What makes the instrument fast to play is the
fixed relationship between three knobs and the active effect — not the colour
of the box.

## 4. Factory content

Do not redistribute samples extracted from the hardware or from Roland's
libraries. Record original factory content from scratch.

Reading a user's own SmartMedia card (`tools/smartmedia/`) is fine — parsing a
data format is not copying firmware — but shipping the contents is not.

## 5. Framework and SDK licences

| Component | Terms | Action |
|---|---|---|
| **JUCE** | Dual-licensed: GPLv3 or commercial. Terms and revenue tiers have changed over time. | **Verify current terms before committing to the framework.** `JUCE_DISPLAY_SPLASH_SCREEN=0` in `plugin/CMakeLists.txt` requires a paid licence. |
| **VST3 SDK** | Steinberg agreement: GPLv3 or proprietary | Must be accepted. Free, but it is an agreement. |
| **AU** | Apple Developer Program | Annual fee |
| **AAX** | Avid partnership + PACE/iLok signing | Significant barrier — deferred past MVP |
| **CLAP** | MIT, no agreement needed | The friction-free option |
| **Catch2** | BSL-1.0 | Test-only, no shipping impact |

**Decision needed before Phase 2:** GPL or commercial JUCE. It determines
whether the plugin can be closed-source, so it cannot be deferred.

## 6. Distribution costs

Budget from day one — lead times are longer than people expect:

- Apple Developer Program — annual, required for notarisation
- Windows EV code-signing certificate — annual, and **issuance takes weeks**
- Without signing: macOS Gatekeeper blocks the install, Windows SmartScreen
  warns users off

---

## Summary

| Item | Status | Blocks |
|---|---|---|
| Firmware boundary | Policy set — measurement only | Phase 0 |
| Product name | **Placeholder, unresolved** | Release |
| Visual identity | Approach set — original design | Phase 5 |
| JUCE licence | **Unresolved** | Phase 2 |
| VST3 agreement | Not yet accepted | Phase 2 |
| Code signing | Not started | Phase 6 |
