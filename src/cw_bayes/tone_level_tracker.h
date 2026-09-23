#pragma once

namespace cw {

// Bounded per-frame reference tracking (dB): the slow release follows a fade
// (QSB) without chasing the key-up edge, and the attack bound rejects
// single-frame power spikes (clicks) that would otherwise cut the mark. The
// attack bound must stay below the classifier's release level.
constexpr float LEVEL_RELEASE_DB = 1.0f;
constexpr float LEVEL_ATTACK_DB  = 6.0f;

// Tracks the amplitude of the mark (key-down) currently being decoded and
// exposes it as a level-relative envelope. It provides the reference for the
// relative decision level of TimeClassifier, which makes the measured
// mark/space timing independent of the signal level.
//
// The reference follows slow fades (QSB) with a bounded per-frame release but
// does not chase the fast key-up edge, its bounded attack rejects single-frame
// power spikes (clicks), and it is re-seeded at the first ON frame of every
// mark. It is kept across the gaps between marks so the classifier can
// interpolate the next front edge; a stale reference is harmless because the
// classifier acquires on an absolute gate, not a relative one.
class ToneLevelTracker {
  public:
    void reset();

    // This frame's mark level in dB relative to the current reference (<= 0).
    // When no reference is established, returns a large positive value so the
    // relative condition never blocks acquisition on its own.
    float norm_db(float power) const;

    // Updates the reference from the new classifier state; `is_on` is the
    // committed mark state. The reference is kept across gaps.
    void observe(float power, bool is_on);

    bool ref_valid() const { return ref_valid_; }

  private:
    float level_     = 0.0f;
    bool  ref_valid_ = false;
    bool  was_on_    = false;
};

} // namespace cw
