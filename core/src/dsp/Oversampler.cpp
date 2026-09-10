#include "sp303/dsp/Oversampler.h"

#include <algorithm>

namespace sp303::dsp {

void Oversampler::prepare(int factor, int maxBlockSize, int numTaps) {
    factor_ = std::max(factor, 1);
    scratch_.assign(static_cast<std::size_t>(std::max(maxBlockSize, 1) * factor_), 0.0f);

    if (factor_ > 1) {
        // Cutoff at the ORIGINAL Nyquist, expressed relative to the
        // oversampled rate.
        const double cutoff = 0.5 / static_cast<double>(factor_);
        upFilter_.designLowpass(cutoff, numTaps);
        downFilter_.designLowpass(cutoff, numTaps);
    }
    reset();
}

void Oversampler::reset() noexcept {
    upFilter_.reset();
    downFilter_.reset();
    std::fill(scratch_.begin(), scratch_.end(), 0.0f);
}

// processSample() is a template and lives in the header - see the comment
// there for why it must not become a std::function.

}  // namespace sp303::dsp
