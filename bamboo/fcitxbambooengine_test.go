// SPDX-License-Identifier: LGPL-2.1-or-later

package main

import (
	"testing"

	"bamboo-core"
)

func newTestEngine() *FcitxBambooEngine {
	return &FcitxBambooEngine{
		preeditor:        bamboo.NewEngine(bamboo.ParseInputMethod(bamboo.InputMethodDefinitions, "Telex"), bamboo.EstdFlags),
		macroTable:       &MacroTable{mTable: map[string]string{}},
		autoNonVnRestore: false,
		outputCharset:    "Unicode",
	}
}

func TestBackspaceRemovesTransformedPreedit(t *testing.T) {
	engine := newTestEngine()
	engine.preeditProcessKeyEvent('d', 0)
	engine.preeditProcessKeyEvent('d', 0)
	if engine.preeditText != "đ" {
		t.Fatalf("preedit after dd = %q, want %q", engine.preeditText, "đ")
	}

	engine.preeditProcessKeyEvent(FcitxBackSpace, 0)
	if engine.preeditText != "" {
		t.Fatalf("preedit after Backspace = %q, want empty", engine.preeditText)
	}
}

func TestEscapeCancelsPreedit(t *testing.T) {
	engine := newTestEngine()
	engine.preeditProcessKeyEvent('d', 0)
	engine.preeditProcessKeyEvent('d', 0)

	engine.preeditProcessKeyEvent(FcitxEscape, 0)
	if engine.preeditText != "" {
		t.Fatalf("preedit after Escape = %q, want empty", engine.preeditText)
	}
	if engine.commitText != "" {
		t.Fatalf("commit after Escape = %q, want empty", engine.commitText)
	}
}

func TestModifiedEscapeDoesNotCancelPreedit(t *testing.T) {
	engine := newTestEngine()
	engine.preeditProcessKeyEvent('d', 0)
	engine.preeditProcessKeyEvent('d', 0)

	if engine.preeditProcessKeyEvent(FcitxEscape, FcitxControlMask) {
		t.Fatal("Ctrl+Escape was consumed")
	}
	if engine.preeditText != "đ" {
		t.Fatalf("preedit after Ctrl+Escape = %q, want %q", engine.preeditText, "đ")
	}
}

func TestRestoreVisibleWordAndContinueTelex(t *testing.T) {
	engine := newTestEngine()
	if !engine.restoreVisibleWord("Đang") {
		t.Fatal("failed to restore visible word")
	}
	if engine.preeditText != "Đang" {
		t.Fatalf("restored preedit = %q, want %q", engine.preeditText, "Đang")
	}

	if !engine.preeditProcessKeyEvent('w', 0) {
		t.Fatal("Telex w was not consumed")
	}
	if engine.preeditText != "Đăng" {
		t.Fatalf("preedit after w = %q, want %q", engine.preeditText, "Đăng")
	}
}

func TestRestoreVisibleWordRejectsInvalidUTF8(t *testing.T) {
	engine := newTestEngine()
	if engine.restoreVisibleWord(string([]byte{0xff})) {
		t.Fatal("invalid UTF-8 word was restored")
	}
}

func TestRestoreFromCommittedTextAndContinueEditing(t *testing.T) {
	engine := newTestEngine()
	engine.restoreFromCommittedText("Đang")

	if engine.preeditText != "Đang" {
		t.Fatalf("preedit after restore = %q, want %q", engine.preeditText, "Đang")
	}
	if engine.preeditProcessKeyEvent('w', 0) == false {
		t.Fatal("processing w after restore failed")
	}
	if engine.preeditText != "Đăng" {
		t.Fatalf("preedit after restored word + w = %q, want %q", engine.preeditText, "Đăng")
	}
}

func TestRestoreVisibleWordTableCases(t *testing.T) {
	cases := []struct {
		word string
		key  rune
		want string
	}{
		{"sao", 's', "sáo"},
		{"Hải", 'w', ""},
		{"hoang", 'f', "hoàng"},
		{"thương", 'j', "thượng"},
		{"thượng", 'j', ""},
		{"Đang", 'w', "Đăng"},
	}
	for _, c := range cases {
		engine := newTestEngine()
		if !engine.restoreVisibleWord(c.word) {
			t.Fatalf("restore %q failed", c.word)
		}
		if engine.preeditText != c.word {
			t.Fatalf("restore %q round-trip = %q", c.word, engine.preeditText)
		}
		if !engine.preeditProcessKeyEvent(uint32(c.key), 0) {
			t.Fatalf("%q + %q not processed", c.word, c.key)
		}
		t.Logf("%q + %q -> %q", c.word, c.key, engine.preeditText)
		if c.want != "" && engine.preeditText != c.want {
			t.Fatalf("%q + %q = %q, want %q", c.word, c.key, engine.preeditText, c.want)
		}
	}
}
