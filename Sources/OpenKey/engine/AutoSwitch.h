/*----------------------------------------------------------
OpenKey - The Cross platform Open source Vietnamese Keyboard application.

AutoSwitch: detects, word by word, which keyboard layout (QWERTY or Dvorak)
and which language (English or Vietnamese Telex) the person is typing in.

It works on physical key positions (scan codes), so the same keystrokes can
be read as QWERTY and as Dvorak at the same time. When a word ends, it
scores each reading against English and Vietnamese word lists and tells the
platform code whether to switch layout and/or retype the word.

This file is platform independent so it can be unit tested anywhere.
Released under GPL license, like the rest of OpenKey.
-----------------------------------------------------------*/
#ifndef AutoSwitch_h
#define AutoSwitch_h

#include <string>
#include <vector>

enum AutoSwitchLayout {
	AUTO_LAYOUT_QWERTY = 0,
	AUTO_LAYOUT_DVORAK = 1,
	AUTO_LAYOUT_UNKNOWN = -1
};

struct AutoSwitchResult {
	bool replace = false;       // delete what is on screen for this word and type `text`
	bool switchLayout = false;  // switch the OS keyboard layout to `layout`
	int layout = AUTO_LAYOUT_UNKNOWN;
	bool vietnamese = false;    // the chosen reading is Vietnamese
	std::u32string text;        // the word as it should appear
};

class AutoSwitch {
public:
	// A printable key went down.
	void pushKey(unsigned int scanCode, bool shift, bool capsLock);
	// Something happened that makes the word unsafe to retype
	// (backspace, arrow keys, mouse click, shortcut...).
	void invalidate();
	// Forget the current word (start of a new word).
	void reset();
	bool hasWord() const { return !_keys.empty(); }
	bool isValid() const { return _valid; }

	// Called after each key: if the word typed so far can not become a word
	// on the current layout but can on the other one, returns the layout to
	// switch to right now (mid-word). Otherwise AUTO_LAYOUT_UNKNOWN.
	int checkEarly(int currentLayout) const;
	// The keys typed so far in this word (for replaying them after a switch).
	size_t keyCount() const { return _keys.size(); }
	unsigned int keyScanCode(size_t i) const { return _keys[i].scanCode; }
	bool keyShift(size_t i) const { return _keys[i].shift; }

	// The word ended (space, enter, tab). Decide what to do, then reset.
	AutoSwitchResult endWord(int currentLayout, bool vietnameseMode, bool allowLayoutSwitch = true);

	// Recent language: > 0 means recent words were Vietnamese, < 0 English.
	int languageContext() const { return _languageContext; }
	void setLanguageContext(int value) { _languageContext = value; }

private:
	struct Key { unsigned int scanCode; bool shift; bool capsLock; };
	std::vector<Key> _keys;
	bool _valid = true;
	int _languageContext = 0;
	int _confirmedWords = 0; // words just typed correctly on the current layout
public:
	// Nobody typed for a while: maybe someone else sat down, so the next
	// word is allowed to switch layout after fewer keys.
	void forgetConfidence() { _confirmedWords = 0; }
	bool isConfident() const { return _confirmedWords > 0; }

	// tuning for checkEarly (zipf * 10): the other reading must reach a word
	// at least this common, and beat the current reading by this margin.
	// Bold right after a pause (first word), careful in the middle of typing.
	int _earlyStrongFirst = 50, _earlyMarginFirst = 20;
	int _earlyStrong = 60, _earlyMargin = 30;
};

// ---- helpers, exposed for tests ----

// Is this scan code one AutoSwitch tracks (letters, digits, punctuation)?
bool autoSwitchIsTrackedKey(unsigned int scanCode);
// The character a key produces on a layout (0 if not tracked).
char32_t autoSwitchCharFor(unsigned int scanCode, bool shift, bool capsLock, int layout);
// Convert a raw Telex word ("vieetj") to Vietnamese ("viet" with marks).
// Returns false when the keys are not a Telex Vietnamese word.
// `key` receives the lookup key: lowercase base letters + tone digit.
bool autoSwitchTelex(const std::u32string& raw, std::u32string& vietnamese, std::string& key);
// Tone placement: false = old style "ho'a" with tone on o (OpenKey default), true = modern, tone on a.
void autoSwitchSetModernOrthography(bool modern);
// Word frequency (zipf * 10, 0 = unknown).
int autoSwitchEnglishScore(const std::u32string& word);
int autoSwitchVietnameseScore(const std::string& key);
// Can these letters still grow into an English word / Vietnamese syllable?
bool autoSwitchCouldBeWord(const std::u32string& letters);
// Highest score of a word these letters could become (0 = none).
int autoSwitchPrefixScore(const std::u32string& letters);

std::string autoSwitchToUtf8(const std::u32string& s);

#endif /* AutoSwitch_h */
