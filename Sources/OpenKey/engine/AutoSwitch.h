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

std::string autoSwitchToUtf8(const std::u32string& s);

#endif /* AutoSwitch_h */
