// Unit tests for engine/AutoSwitch. Build and run:
//   g++ -std=c++17 -O1 -I Sources/OpenKey/engine tests/autoswitch_test.cpp Sources/OpenKey/engine/AutoSwitch.cpp -o /tmp/autoswitch_test && /tmp/autoswitch_test
#include "AutoSwitch.h"

#include <cstdio>
#include <string>

static int failures = 0, checks = 0;

static void check(bool ok, const std::string& what) {
	checks++;
	if (!ok) { failures++; printf("FAIL: %s\n", what.c_str()); }
}

static std::u32string u32(const char* utf8) {
	std::u32string out;
	const unsigned char* p = (const unsigned char*)utf8;
	while (*p) {
		char32_t c;
		if (*p < 0x80) c = *p++;
		else if ((*p & 0xE0) == 0xC0) { c = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F); p += 2; }
		else if ((*p & 0xF0) == 0xE0) { c = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); p += 3; }
		else { c = ((p[0] & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F); p += 4; }
		out += c;
	}
	return out;
}

// Press the keys that produce `text` on `layout` (what a person used to that layout types).
static void typeAs(AutoSwitch& as, const char* text, int layout) {
	for (const char* p = text; *p; p++) {
		bool found = false;
		for (unsigned int sc = 0; sc < 0x60 && !found; sc++) {
			for (int shift = 0; shift < 2 && !found; shift++) {
				if (autoSwitchCharFor(sc, shift, false, layout) == (char32_t)(unsigned char)*p) {
					as.pushKey(sc, shift, false);
					found = true;
				}
			}
		}
		if (!found) printf("no key for '%c'\n", *p);
	}
}

static AutoSwitchResult word(AutoSwitch& as, const char* text, int typedLayout, int activeLayout, bool vnMode = true) {
	typeAs(as, text, typedLayout);
	return as.endWord(activeLayout, vnMode);
}

static void telexCase(const char* raw, const char* expected) {
	std::u32string out; std::string key;
	bool ok = autoSwitchTelex(u32(raw), out, key);
	check(expected ? (ok && out == u32(expected)) : !ok,
	      std::string("telex ") + raw + " -> " + (ok ? autoSwitchToUtf8(out) : "(invalid)") + ", expected " + (expected ? expected : "(invalid)"));
}

int main() {
	const int Q = AUTO_LAYOUT_QWERTY, D = AUTO_LAYOUT_DVORAK;

	// --- layout tables
	check(autoSwitchCharFor(0x1F, false, false, Q) == 's', "scan 0x1F is s on QWERTY");
	check(autoSwitchCharFor(0x1F, false, false, D) == 'o', "scan 0x1F is o on Dvorak");
	check(autoSwitchCharFor(0x10, true, false, D) == '"', "shift+scan 0x10 is \" on Dvorak");
	check(autoSwitchCharFor(0x10, false, true, Q) == 'Q', "caps+scan 0x10 is Q on QWERTY");
	check(autoSwitchCharFor(0x10, false, true, D) == '\'', "caps does not change ' on Dvorak");

	// --- Telex
	telexCase("vieetj", "việt");
	telexCase("Vieetj", "Việt");
	telexCase("nguwowif", "người");
	telexCase("dduowcj", "được");
	telexCase("khoong", "không");
	telexCase("hoaf", "hòa");
	telexCase("hoas", "hóa");
	telexCase("tieengs", "tiếng");
	telexCase("quas", "quá");
	autoSwitchSetModernOrthography(true);
	telexCase("hoaf", "hoà");
	telexCase("thuyr", "thuỷ");
	autoSwitchSetModernOrthography(false);
	telexCase("thuyr", "thủy");
	telexCase("giaf", "già");
	telexCase("muaw", "muă");
	telexCase("as", "á");
	telexCase("ass", nullptr);
	telexCase("tooi", "tôi");
	telexCase("w", "ư");

	// --- English stays English, Vietnamese stays Vietnamese (same layout)
	{
		AutoSwitch as;
		AutoSwitchResult r = word(as, "hello", Q, Q);
		check(!r.replace && !r.switchLayout && !r.vietnamese, "hello on QWERTY: nothing to do");
		r = word(as, "vieetj", Q, Q);
		check(!r.replace && r.vietnamese && r.text == u32("việt"), "vieetj on QWERTY: Vietnamese, no retype");
	}
	// --- English words that are also Telex
	{
		AutoSwitch as;
		AutoSwitchResult r = word(as, "is", Q, Q);
		check(r.replace && !r.vietnamese && r.text == u32("is"), "'is' is restored to English");
		r = word(as, "as", Q, Q);
		check(r.replace && r.text == u32("as"), "'as' is restored to English");
	}
	{
		AutoSwitch as;
		word(as, "this", Q, Q);
		word(as, "is", Q, Q);
		word(as, "my", Q, Q);
		AutoSwitchResult r = word(as, "car", Q, Q);
		check(r.replace && !r.vietnamese && r.text == u32("car"), "'car' after English words stays English");
	}
	{
		AutoSwitch as;
		word(as, "tooi", Q, Q);
		word(as, "muoons", Q, Q);
		word(as, "mua", Q, Q);
		AutoSwitchResult r = word(as, "car", Q, Q);
		check(!r.replace && r.vietnamese && r.text == u32("cả"), "'car' after Vietnamese words becomes cả");
	}
	// --- Layout detection: wife (QWERTY habits) types while Dvorak is active
	{
		AutoSwitch as;
		AutoSwitchResult r = word(as, "hello", Q, D);
		check(r.switchLayout && r.layout == Q && r.replace && r.text == u32("hello"), "QWERTY 'hello' on Dvorak -> switch to QWERTY");
		r = word(as, "hello,", Q, D);
		check(r.switchLayout && r.text == u32("hello,"), "QWERTY 'hello,' on Dvorak keeps the comma");
		r = word(as, "khoong", Q, D);
		check(r.switchLayout && r.vietnamese && r.text == u32("không"), "QWERTY 'khoong' on Dvorak -> không + switch");
		r = word(as, "nguwowif", Q, D);
		check(r.switchLayout && r.text == u32("người"), "QWERTY 'nguwowif' on Dvorak -> người");
	}
	// --- Layout detection: Huy (Dvorak habits) types while QWERTY is active
	{
		AutoSwitch as;
		AutoSwitchResult r = word(as, "keyboard", D, Q);
		check(r.switchLayout && r.layout == D && r.text == u32("keyboard"), "Dvorak 'keyboard' on QWERTY -> switch to Dvorak");
		r = word(as, "vieetj", D, Q);
		check(r.switchLayout && r.layout == D && r.text == u32("việt"), "Dvorak 'vieetj' on QWERTY -> việt");
		r = word(as, "Please", D, Q);
		check(r.switchLayout && r.text == u32("Please"), "Dvorak 'Please' keeps capital");
	}
	// --- No false switches for correct typing
	{
		const char* en[] = { "the", "people", "would", "think", "about", "computer", "language", "thanks", "okay", "wife" };
		const char* vn[] = { "khoong", "dduowcj", "nguwowif", "laf", "cuar", "tieengs", "vieetj", "nhaf", "ddi", "awn" };
		for (int layout = 0; layout < 2; layout++) {
			AutoSwitch as;
			for (const char* w : en) { AutoSwitchResult r = word(as, w, layout, layout); check(!r.switchLayout, std::string("no switch for ") + w); }
			for (const char* w : vn) { AutoSwitchResult r = word(as, w, layout, layout); check(!r.switchLayout, std::string("no switch for ") + w); }
		}
	}
	// --- Invalidated words are left alone
	{
		AutoSwitch as;
		typeAs(as, "hello", Q);
		as.invalidate();
		AutoSwitchResult r = as.endWord(D, true);
		check(!r.replace && !r.switchLayout, "invalidated word: nothing");
	}
	// --- English mode never produces Vietnamese, still switches layout
	{
		AutoSwitch as;
		AutoSwitchResult r = word(as, "as", Q, Q, false);
		check(!r.replace, "English mode: 'as' untouched");
		r = word(as, "hello", Q, D, false);
		check(r.switchLayout && r.text == u32("hello"), "English mode: still switches layout");
	}

	// --- Early (mid-word) switch
	{
		auto earlyAt = [](const char* text, int typed, int active) {
			AutoSwitch as;
			char buf[2] = { 0, 0 };
			for (int i = 0; text[i]; i++) {
				buf[0] = text[i];
				typeAs(as, buf, typed);
				if (as.checkEarly(active) != AUTO_LAYOUT_UNKNOWN) return i + 1;
			}
			return 0;
		};
		int n = earlyAt("hello", Q, D);
		check(n >= 2 && n <= 3, "QWERTY 'hello' on Dvorak switches by the 3rd key, got " + std::to_string(n));
		n = earlyAt("khoong", Q, D);
		check(n >= 2 && n <= 4, "QWERTY 'khoong' on Dvorak switches early, got " + std::to_string(n));
		n = earlyAt("keyboard", D, Q);
		check(n >= 2 && n <= 4, "Dvorak 'keyboard' on QWERTY switches early, got " + std::to_string(n));
		check(earlyAt("hello", Q, Q) == 0, "QWERTY 'hello' on QWERTY never switches early");
		check(earlyAt("nguwowif", D, D) == 0, "Dvorak 'nguwowif' on Dvorak never switches early");
		AutoSwitch as;
		typeAs(as, "he", Q);
		check(as.keyCount() == 2 && as.keyScanCode(0) == 0x23, "keys are kept for replay");
	}

	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
