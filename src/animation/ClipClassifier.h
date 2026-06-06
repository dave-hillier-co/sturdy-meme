#pragma once

#include <string>
#include <cctype>

// Single source of truth for classifying animation clips by name.
//
// Several systems need to bucket clips into locomotion categories from their
// names (idle/walk/run/strafe/turn/jump). Historically this substring matching
// was copy-pasted into AnimatedCharacter's state-machine setup, its
// loadAdditionalAnimations refresh, and motion-matching database population,
// which let the variants drift apart. Centralising it here keeps those callers
// (and tests) in agreement.
namespace anim {

enum class ClipCategory {
    Unknown,
    Idle,
    Walk,
    Run,
    Strafe,      // includes "backward" locomotion
    Turn,
    Jump,
    Transition,  // one-shot start/stop clips (e.g. "walk_start", "run_stop")
};

struct ClipClassification {
    ClipCategory category = ClipCategory::Unknown;
    bool looping = false;      // whether the clip should loop when played
    bool isVariant = false;    // alternate take (e.g. "idle2", "run_alt")
    bool isStartStop = false;  // one-shot transition clip
    bool isMetadata = false;   // placeholder/empty clip that should be skipped
};

inline std::string toLowerCopy(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

inline bool nameContains(const std::string& lowerName, const char* token) {
    return lowerName.find(token) != std::string::npos;
}

// Classify a clip from its (case-insensitive) name. `duration` is used only to
// detect placeholder/metadata clips; pass a value >= 0.1 if unknown.
//
// Ordering matters: more specific patterns are checked before generic ones
// (start/stop before base locomotion, backward/run before walk).
inline ClipClassification classifyClip(const std::string& name, float duration = 1.0f) {
    ClipClassification c;
    const std::string lower = toLowerCopy(name);

    if (lower == "mixamo.com" || lower.empty() || duration < 0.1f) {
        c.isMetadata = true;
        return c;
    }

    c.isStartStop = nameContains(lower, "start") || nameContains(lower, "stop");
    c.isVariant = nameContains(lower, "2") || nameContains(lower, "_2") ||
                  nameContains(lower, "alt");

    if (c.isStartStop) {
        c.category = ClipCategory::Transition;
        c.looping = false;
    } else if (nameContains(lower, "idle")) {
        c.category = ClipCategory::Idle;
        c.looping = true;
    } else if (nameContains(lower, "backward")) {
        // Backward walk/run — checked before generic walk/run.
        c.category = ClipCategory::Strafe;
        c.looping = true;
    } else if (nameContains(lower, "run")) {
        // Checked before walk since "run" can appear in compound names.
        c.category = ClipCategory::Run;
        c.looping = true;
    } else if (nameContains(lower, "walk")) {
        c.category = ClipCategory::Walk;
        c.looping = true;
    } else if (nameContains(lower, "strafe")) {
        c.category = ClipCategory::Strafe;
        c.looping = true;
    } else if (nameContains(lower, "turn")) {
        c.category = ClipCategory::Turn;
        c.looping = false;
    } else if (nameContains(lower, "jump")) {
        c.category = ClipCategory::Jump;
        c.looping = false;
    }

    return c;
}

} // namespace anim
