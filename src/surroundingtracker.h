/*
 * SPDX-FileCopyrightText: 2026 fcitx5-bamboo contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#ifndef _FCITX5_BAMBOO_SURROUNDINGTRACKER_H_
#define _FCITX5_BAMBOO_SURROUNDINGTRACKER_H_

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>

namespace fcitx::bamboo_reedit {

// Number of characters kept on each side of the cursor.
inline constexpr size_t kViewContext = 64;
inline constexpr size_t kMaxReeditWordLength = 7;

struct TextView {
    bool valid = false;
    std::u32string before;
    std::u32string after;

    bool operator==(const TextView &other) const {
        return valid == other.valid && before == other.before &&
               after == other.after;
    }
    bool operator!=(const TextView &other) const { return !(*this == other); }
};

inline TextView makeTextView(const std::u32string &text, size_t cursor,
                             size_t anchor) {
    TextView view;
    if (cursor != anchor || cursor > text.size()) {
        return view;
    }
    view.valid = true;
    const size_t beforeStart = cursor > kViewContext ? cursor - kViewContext : 0;
    view.before = text.substr(beforeStart, cursor - beforeStart);
    view.after = text.substr(cursor, kViewContext);
    return view;
}

inline bool isLetterCharacter(char32_t chr) {
    return (chr >= U'a' && chr <= U'z') || (chr >= U'A' && chr <= U'Z') ||
           (chr >= 0x00C0 && chr <= 0x00D6) ||
           (chr >= 0x00D8 && chr <= 0x00F6) ||
           (chr >= 0x00F8 && chr <= 0x01B0) ||
           (chr >= 0x1EA0 && chr <= 0x1EF9);
}

inline bool isWordJoiner(char32_t chr) {
    return isLetterCharacter(chr) || (chr >= U'0' && chr <= U'9') ||
           chr == U'_' || (chr >= 0x0300 && chr <= 0x036F);
}

// The word ending exactly at the cursor, if the cursor is at a clean word end.
inline std::optional<std::u32string> wordEndingAtCursor(const TextView &view) {
    if (!view.valid || view.before.empty()) {
        return std::nullopt;
    }
    if (!view.after.empty() && isWordJoiner(view.after.front())) {
        return std::nullopt;
    }
    const size_t cursor = view.before.size();
    size_t start = cursor;
    while (start > 0 && isLetterCharacter(view.before[start - 1])) {
        start--;
        if (cursor - start > kMaxReeditWordLength) {
            return std::nullopt;
        }
    }
    if (start == cursor) {
        return std::nullopt;
    }
    if (start > 0 && isWordJoiner(view.before[start - 1])) {
        return std::nullopt;
    }
    if (start == 0 && view.before.size() >= kViewContext) {
        return std::nullopt;
    }
    return view.before.substr(start);
}

// Tracks whether the client's surrounding text can be trusted. Every change
// we cause (commit, forwarded key, deletion) is predicted; the client report
// is only trusted again once it matches the prediction, so stale or delayed
// reports can never be used to edit text.
class SurroundingTracker {
public:
    enum class State { Fresh, Predict, AnyChange };

    void clientUpdated(const TextView &view) {
        lastSeen_ = view;
        switch (state_) {
        case State::Fresh:
            verified_ = view;
            break;
        case State::Predict:
            if (matches(view, predicted_)) {
                state_ = State::Fresh;
                verified_ = view;
            } else {
                mismatchSincePredict_ = true;
            }
            break;
        case State::AnyChange:
            if (view.valid && view != snapshot_) {
                state_ = State::Fresh;
                verified_ = view;
            }
            break;
        }
    }

    void inserted(const std::u32string &text) {
        apply([&text](TextView &view) {
            view.before += text;
            trimBefore(view);
            return true;
        });
    }

    void backspaced() {
        apply([](TextView &view) {
            if (!view.before.empty()) {
                view.before.pop_back();
            }
            return true;
        });
    }

    void deletedBefore(size_t count) {
        apply([count](TextView &view) {
            if (view.before.size() < count) {
                return false;
            }
            view.before.erase(view.before.size() - count);
            return true;
        });
    }

    void unknownChange() { waitForAnyChange(); }

    const TextView *verified() const {
        return state_ == State::Fresh && verified_.valid ? &verified_
                                                         : nullptr;
    }

    State state() const { return state_; }

private:
    static void trimBefore(TextView &view) {
        if (view.before.size() > kViewContext) {
            view.before.erase(0, view.before.size() - kViewContext);
        }
    }

    static bool suffixMatch(const std::u32string &a, const std::u32string &b) {
        const size_t n = std::min(a.size(), b.size());
        if (a.size() != b.size() && n < kViewContext / 2) {
            return false;
        }
        return a.compare(a.size() - n, n, b, b.size() - n, n) == 0;
    }

    static bool prefixMatch(const std::u32string &a, const std::u32string &b) {
        const size_t n = std::min(a.size(), b.size());
        if (a.size() != b.size() && n < kViewContext / 2) {
            return false;
        }
        return a.compare(0, n, b, 0, n) == 0;
    }

    static bool matches(const TextView &view, const TextView &predicted) {
        return view.valid && predicted.valid &&
               suffixMatch(view.before, predicted.before) &&
               prefixMatch(view.after, predicted.after);
    }

    void waitForAnyChange() {
        state_ = State::AnyChange;
        snapshot_ = lastSeen_;
        mismatchSincePredict_ = false;
    }

    template <typename Op>
    void apply(Op op) {
        TextView base;
        if (state_ == State::Fresh) {
            base = verified_;
        } else if (state_ == State::Predict && !mismatchSincePredict_) {
            base = predicted_;
        } else {
            waitForAnyChange();
            return;
        }
        if (!base.valid || !op(base)) {
            waitForAnyChange();
            return;
        }
        predicted_ = std::move(base);
        state_ = State::Predict;
        mismatchSincePredict_ = false;
    }

    State state_ = State::AnyChange;
    TextView lastSeen_;
    TextView snapshot_;
    TextView verified_;
    TextView predicted_;
    bool mismatchSincePredict_ = false;
};

} // namespace fcitx::bamboo_reedit

#endif // _FCITX5_BAMBOO_SURROUNDINGTRACKER_H_
