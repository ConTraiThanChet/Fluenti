/*
 * SPDX-FileCopyrightText: 2026 fcitx5-bamboo contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#include "surroundingtracker.h"
#include <cstdlib>
#include <iostream>

using namespace fcitx::bamboo_reedit;

namespace {

int failures = 0;

void check(bool condition, const char *what) {
    if (!condition) {
        std::cerr << "FAILED: " << what << std::endl;
        failures++;
    }
}

TextView at(const std::u32string &text) {
    return makeTextView(text, text.size(), text.size());
}

std::optional<std::u32string> reeditWord(const SurroundingTracker &tracker,
                                         const std::u32string &clientText) {
    const auto *view = tracker.verified();
    if (!view || *view != at(clientText)) {
        return std::nullopt;
    }
    return wordEndingAtCursor(*view);
}

void testSpaceBackspaceThenLetter() {
    SurroundingTracker tracker;
    tracker.clientUpdated(at(U"chính "));
    tracker.inserted(U"thức ");
    tracker.clientUpdated(at(U"chính thức "));
    tracker.backspaced();
    tracker.clientUpdated(at(U"chính thức"));
    auto word = reeditWord(tracker, U"chính thức");
    check(word && *word == U"thức", "word after space + backspace");
}

void testDelayedStaleUpdateIsRejected() {
    // "chính thức" Space, Backspace, Space, n -> must not re-edit "thức".
    SurroundingTracker tracker;
    tracker.clientUpdated(at(U"chính "));
    tracker.inserted(U"thức ");
    tracker.clientUpdated(at(U"chính thức "));
    tracker.backspaced();
    tracker.inserted(U" ");
    // Report for the Backspace arrives only after Space was forwarded.
    tracker.clientUpdated(at(U"chính thức"));
    check(!tracker.verified(), "stale update must not be trusted");
    check(!reeditWord(tracker, U"chính thức"), "no re-edit from stale text");
    tracker.clientUpdated(at(U"chính thức "));
    check(tracker.verified() != nullptr, "matching update is trusted");
    check(!reeditWord(tracker, U"chính thức "), "no word after a space");
}

void testTwoSpacesTwoBackspaces() {
    SurroundingTracker tracker;
    tracker.clientUpdated(at(U""));
    tracker.inserted(U"Đang ");
    tracker.inserted(U" ");
    tracker.backspaced();
    tracker.backspaced();
    tracker.clientUpdated(at(U"Đang "));
    check(!tracker.verified(), "intermediate update is not trusted");
    tracker.clientUpdated(at(U"Đang"));
    auto word = reeditWord(tracker, U"Đang");
    check(word && *word == U"Đang", "re-edit after two backspaces");
}

void testRepeatedReedit() {
    SurroundingTracker tracker;
    tracker.clientUpdated(at(U"hoàng thượng "));
    tracker.backspaced();
    tracker.clientUpdated(at(U"hoàng thượng"));
    auto word = reeditWord(tracker, U"hoàng thượng");
    check(word && *word == U"thượng", "first re-edit word");
    tracker.deletedBefore(word->size());
    check(!tracker.verified(), "deletion waits for client");
    tracker.clientUpdated(at(U"hoàng "));
    check(tracker.verified() != nullptr, "deletion confirmed");
    tracker.inserted(U"thượng ");
    tracker.clientUpdated(at(U"hoàng thượng "));
    tracker.backspaced();
    tracker.clientUpdated(at(U"hoàng thượng"));
    word = reeditWord(tracker, U"hoàng thượng");
    check(word && *word == U"thượng", "second re-edit word");
}

void testUnknownChangeNeedsNewReport() {
    SurroundingTracker tracker;
    tracker.clientUpdated(at(U"abc def"));
    tracker.unknownChange();
    check(!tracker.verified(), "unknown change is not trusted");
    tracker.clientUpdated(at(U"abc def"));
    check(!tracker.verified(), "unchanged report is not trusted");
    tracker.clientUpdated(makeTextView(U"abc def", 3, 3));
    check(tracker.verified() != nullptr, "changed report is trusted");
}

void testWordRules() {
    check(!wordEndingAtCursor(makeTextView(U"Đang", 2, 2)), "mid word");
    check(!wordEndingAtCursor(at(U"abc1")), "digit before cursor");
    check(!wordEndingAtCursor(at(U"1abc")), "digit joins word");
    check(!wordEndingAtCursor(at(U"nghiêngg")), "too long");
    check(!wordEndingAtCursor(makeTextView(U"Đang", 4, 0)), "selection");
    auto word = wordEndingAtCursor(at(U"(Đăng"));
    check(word && *word == U"Đăng", "punctuation boundary");
}

} // namespace

int main() {
    testSpaceBackspaceThenLetter();
    testDelayedStaleUpdateIsRejected();
    testTwoSpacesTwoBackspaces();
    testRepeatedReedit();
    testUnknownChangeNeedsNewReport();
    testWordRules();
    if (failures) {
        return EXIT_FAILURE;
    }
    std::cout << "All surrounding tracker tests passed" << std::endl;
    return EXIT_SUCCESS;
}
