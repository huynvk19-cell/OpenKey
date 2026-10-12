// End-to-end Vietnamese typing simulation for NhaKey.
//
// Runs OpenKey's real Telex engine (engine/Engine.cpp) and NhaKey's auto
// switch logic the same way the Windows keyboard hook does (win32/.../OpenKey.cpp),
// on a simulated screen, and checks the text that ends up on screen.
//
// Samples: Vietnamese sentences made of syllables drawn from the built-in
// list (weighted by how common they are), typed with several Telex habits,
// by a QWERTY person and by a Dvorak person, on the right and on the wrong
// layout. The expected text is what OpenKey's engine alone types on the right
// layout, so only NhaKey's own mistakes are counted.
//
// Build and run (from the repository root):
//   g++ -std=c++17 -O2 -DLINUX -include algorithm -I Sources/OpenKey/engine tests/vietnamese_sim.cpp \
//     Sources/OpenKey/engine/AutoSwitch.cpp Sources/OpenKey/engine/Engine.cpp Sources/OpenKey/engine/Vietnamese.cpp \
//     Sources/OpenKey/engine/Macro.cpp Sources/OpenKey/engine/SmartSwitchKey.cpp Sources/OpenKey/engine/ConvertTool.cpp \
//     -o /tmp/vietnamese_sim
//   /tmp/vietnamese_sim [sentences=200] [seed=2026] [csv file]
// Add -DOLD_PLATFORM to replay the hook as it was before the Vietnamese fixes
// (with the old engine/AutoSwitch sources) to compare.
#include "Engine.h"
#include "AutoSwitch.h"
#include "AutoSwitchData.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

// engine settings, as NhaKey sets them (OpenKeyInit)
int vLanguage = 1, vInputType = 0, vFreeMark = 0, vCodeTable = 0, vCheckSpelling = 1;
int vUseModernOrthography = 0, vQuickTelex = 0, vRestoreIfWrongSpelling = 1, vUseMacro = 0;
int vAutoCapsMacro = 0, vUpperCaseFirstChar = 0, vAllowConsonantZFWJ = 0;
int vQuickStartConsonant = 0, vQuickEndConsonant = 0;

static const int Q = AUTO_LAYOUT_QWERTY, D = AUTO_LAYOUT_DVORAK;

static std::u32string u32(const std::string& utf8) {
	std::u32string out;
	const unsigned char* p = (const unsigned char*)utf8.c_str();
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
static std::string utf8(const std::u32string& s) { return autoSwitchToUtf8(s); }

struct Key { unsigned int scanCode; bool shift; };

// the key (and shift) that types `ch` on `layout`
static bool keyFor(char ch, int layout, Key& key) {
	for (int shift = 0; shift < 2; shift++)
		for (unsigned int sc = 0; sc < 0x60; sc++)
			if (autoSwitchCharFor(sc, shift, false, layout) == (char32_t)(unsigned char)ch) { key = { sc, shift != 0 }; return true; }
	return false;
}

// Linux engine key codes are scan code + 8, on the QWERTY position of the character
static Uint16 engineKeyCode(char32_t base) {
	for (unsigned int sc = 0; sc < 0x60; sc++)
		if (autoSwitchCharFor(sc, false, false, Q) == base) return (Uint16)(sc + 8);
	return 0;
}

// ------------------------------------------------------------ the simulated PC
struct Machine {
	bool autoSwitch = true;      // NhaKey on (false = OpenKey engine alone)
#ifdef OLD_PLATFORM
	static const bool oldPlatform = true;  // the hook as it was before the Vietnamese fixes
#else
	static const bool oldPlatform = false;
#endif
	int layout = Q;              // the layout keys are read with
	std::u32string screen;
	vKeyHookState* pData = nullptr;
	AutoSwitch as;
	int screenCount = 0, previousWordScreen = -1;
	int switches = 0;

	Machine() { pData = (vKeyHookState*)vKeyInit(); startNewSession(); }

	// like CountedSendInput: what NhaKey types is counted, except the space key itself
	void put(char32_t c, bool counted = true) { screen += c; if (counted) screenCount++; }
	void backspace() { if (!screen.empty()) screen.pop_back(); screenCount--; }
	void typeText(const std::u32string& t) { for (char32_t c : t) put(c); }

	void sendKeyCode(Uint32 data) {
		if (!(data & CHAR_CODE_MASK)) {
			Uint16 ch = keyCodeToCharacter(data);
			if (ch) { put(ch, ch != U' '); return; }
			if ((data & 0xFFFF) == KEY_SPACE) { put(U' ', false); return; }
			Uint32 code = data & 0xFFFF;
			if (code >= 8) { char32_t c = autoSwitchCharFor(code - 8, (data & CAPS_MASK) != 0, false, Q); if (c) put(c); }
			return;
		}
		put((char32_t)(Uint16)data);
	}

	// OpenKey's own handling of one key (keyboardHookProcessCore); returns true if it passed through
	bool core(Uint16 keyCode, bool shift, char32_t ch) {
		vKeyHandleEvent(vKeyEvent::Keyboard, vKeyEventState::KeyDown, keyCode, shift ? 1 : 0, false);
		if (pData->code == vDoNothing) { if (ch) put(ch, ch != U' '); return true; }
		if (pData->code == vWillProcess || pData->code == vRestore || pData->code == vRestoreAndStartNewSession) {
			for (int i = 0; i < pData->backspaceCount && pData->backspaceCount < MAX_BUFF; i++) backspace();
			for (int i = pData->newCharCount - 1; i >= 0 && pData->newCharCount <= MAX_BUFF; i--) sendKeyCode(pData->charData[i]);
			if (pData->code == vRestore || pData->code == vRestoreAndStartNewSession)
				sendKeyCode(keyCode | (shift ? CAPS_MASK : 0));
			if (pData->code == vRestoreAndStartNewSession) startNewSession();
		}
		return false;
	}

	void letter(const Key& k, bool replayed = false) {
		if (autoSwitch) {
			as.pushKey(k.scanCode, k.shift, false);
			if (!replayed) {
				int target = as.checkEarly(layout);
				if (target != AUTO_LAYOUT_UNKNOWN) { replay(target); return; }
			}
		}
		char32_t base = autoSwitchCharFor(k.scanCode, false, false, layout);
		char32_t ch = autoSwitchCharFor(k.scanCode, k.shift, false, layout);
		core(engineKeyCode(base), k.shift, ch);
	}

	void replay(int target) {
		std::vector<Key> keys;
		for (size_t i = 0; i < as.keyCount(); i++) keys.push_back({ as.keyScanCode(i), as.keyShift(i) });
		int del = screenCount > 0 && screenCount <= 64 ? screenCount : 0;
		std::u32string previousText;
		bool fixPrevious = !oldPlatform && previousWordScreen > 0 && as.previousWordFix(target, vLanguage == 1, previousText);
		if (fixPrevious) del += previousWordScreen + 1;
		for (int i = 0; i < del; i++) backspace();
		if (fixPrevious) { typeText(previousText); put(U' ', false); }
		previousWordScreen = -1;
		screenCount = 0;
		startNewSession();
		as.reset();
		as.breakPair();
		as.setLanguageContext(0);
		if (!oldPlatform) as.markEarlySwitch(layout);
		layout = target;
		switches++;
		for (const Key& k : keys) letter(k, true);
	}

	bool wordEnd(bool space) {
		autoSwitchSetModernOrthography(vUseModernOrthography != 0);
		AutoSwitchResult r = as.endWord(layout, vLanguage == 1, true);
		if (!space) as.breakPair();
		if (r.switchLayout) { layout = r.layout; switches++; }
		int wordScreen = screenCount;
		if (!r.replace || screenCount <= 0 || screenCount > 64) {
			previousWordScreen = space && wordScreen > 0 && wordScreen <= 64 ? wordScreen : -1;
			return false;
		}
		int del = screenCount;
		bool fixPrevious = r.replacePrevious && previousWordScreen > 0;
		if (fixPrevious) del += previousWordScreen + 1;
		for (int i = 0; i < del; i++) backspace();
		if (fixPrevious) { typeText(r.previousText); typeText(U" "); }
		typeText(r.text);
		startNewSession();
		previousWordScreen = space ? (int)r.text.size() : -1;
		return true;
	}

	void space() {
		bool hadWord = as.hasWord();
		if (autoSwitch && hadWord && wordEnd(true)) {
			as.reset(); screenCount = 0;
			put(U' ', false);
			return;
		}
		if (autoSwitch && (oldPlatform ? !as.hasWord() : !hadWord)) { as.breakPair(); previousWordScreen = -1; } // two spaces in a row
		if (autoSwitch) as.reset();
		screenCount = 0;
		core(KEY_SPACE, false, U' ');
		if (oldPlatform) return; // the old hook kept counting into the next word
		// OpenKey may rewrite the word at the space (spelling restore): keep its length right
		if (previousWordScreen > 0) previousWordScreen += screenCount;
		if (previousWordScreen <= 0 || previousWordScreen > 64) previousWordScreen = -1;
		screenCount = 0;
	}

	// type `text` with the habits of `habit` (ASCII Telex keys and spaces)
	void type(const std::string& text, int habit) {
		for (char c : text) {
			if (c == ' ') { space(); continue; }
			Key k;
			if (keyFor(c, habit, k)) letter(k);
		}
	}
};

// ------------------------------------------------------------ Telex habits
// "<base letters><tone digit>" -> keys. Styles:
enum Style { TONE_END, TONE_AFTER_VOWEL, W_ALONE, MARK_AT_END, STYLE_COUNT };
static const char* kStyleNames[STYLE_COUNT] = {
	"tone at the end (tieengs)", "tone after the vowel (tieesng)", "w for u-horn (nhwngx, dwowcj)", "marks at the end (tiengse)" };

static bool telexKeys(const std::string& key, Style style, std::string& out) {
	std::u32string base = u32(key.substr(0, key.size() - 1));
	int tone = key.back() - '0';
	const char* toneKey = tone ? std::string(" sfrxj").substr(tone, 1).c_str() : "";
	std::string toneStr = tone ? std::string(1, " sfrxj"[tone]) : std::string();
	(void)toneKey;
	std::string letters, marks;
	int lastVowel = -1;
	auto isV = [](char32_t c) { return std::u32string(U"aăâeêioôơuưy").find(c) != std::u32string::npos; };
	out.clear();
	for (size_t i = 0; i < base.size(); i++) {
		char32_t c = base[i];
		std::string plain, mark;
		if (c == U'ư' && i + 1 < base.size() && base[i + 1] == U'ơ') {
			if (style == W_ALONE) plain = "wow"; // "dwowcj"
			else { plain = "uo"; mark = "w"; }
			i++;
		} else switch (c) {
			case U'ă': plain = "a"; mark = "w"; break;
			case U'â': plain = "a"; mark = "a"; break;
			case U'đ': plain = "d"; mark = "d"; break;
			case U'ê': plain = "e"; mark = "e"; break;
			case U'ô': plain = "o"; mark = "o"; break;
			case U'ơ': plain = "o"; mark = "w"; break;
			case U'ư': if (style == W_ALONE) plain = "w"; else { plain = "u"; mark = "w"; } break;
			default: if (c >= 'a' && c <= 'z') plain = std::string(1, (char)c); else return false;
		}
		if (style == MARK_AT_END) { letters += plain; marks += mark; }
		else { letters += plain + mark; }
		if (isV(c) || c == U'ơ') lastVowel = (int)letters.size();
	}
	if (style == MARK_AT_END) {
		// "dd" stays at the start, other marks after the final consonant
		if (!marks.empty() && marks[0] == 'd' && letters[0] == 'd') { letters.insert(1, "d"); marks.erase(0, 1); }
		out = letters + marks + toneStr;
	} else if (style == TONE_AFTER_VOWEL && lastVowel >= 0) {
		out = letters.substr(0, lastVowel) + toneStr + letters.substr(lastVowel);
	} else {
		out = letters + toneStr;
	}
	return true;
}

// ------------------------------------------------------------ benchmark
static std::u32string engineAlone(const std::string& keys, int habit) {
	Machine m; m.autoSwitch = false; m.layout = habit;
	m.type(keys, habit);
	return m.screen;
}

// a syllable the engine alone types right with this habit (Telex keys)
static bool syllableKeys(const std::string& syllable, Style style, std::string& keys) {
	if (!telexKeys(syllable, style, keys)) return false;
	std::u32string vn; std::string key;
	return autoSwitchTelex(u32(keys), vn, key) && key == syllable && engineAlone(keys + " ", Q) == vn + U" ";
}

static const char* kOnsets[] = { "ngh", "ng", "nh", "ch", "gh", "gi", "kh", "ph", "qu", "th", "tr",
	"b", "c", "d", "\xC4\x91", "g", "h", "k", "l", "m", "n", "p", "r", "s", "t", "v", "x" };

static size_t onsetLength(const std::string& key) {
	for (const char* o : kOnsets)
		if (key.compare(0, strlen(o), o) == 0 && key.size() > strlen(o) + 1) return strlen(o);
	return 0;
}

static bool inWordList(const std::string& key) {
	int lo = 0, hi = kVietnameseSyllablesCount - 1;
	while (lo <= hi) {
		int mid = (lo + hi) / 2, c = strcmp(kVietnameseSyllables[mid].word, key.c_str());
		if (c == 0) return true;
		if (c < 0) lo = mid + 1; else hi = mid - 1;
	}
	return false;
}

// Vietnamese spelling: k / gh / ngh before e, ê, i (y); c / g / ng elsewhere; qu not before u, o
static bool spelledRight(const std::string& onset, const std::string& rhyme) {
	char32_t v = u32(rhyme)[0];
	bool front = v == 'e' || v == U'\u00EA' || v == 'i' || v == 'y';
	bool frontE = v == 'e' || v == U'\u00EA' || v == 'i';
	if (onset == "c") return !front;
	if (onset == "k") return front;
	if (onset == "g") return v != 'e' && v != U'\u00EA';
	if (onset == "gh" || onset == "ngh") return frontE;
	if (onset == "ng") return !frontE;
	if (onset == "gi") return v != 'i';
	if (onset == "qu") return v != 'u' && v != U'\u01B0' && v != 'o' && v != U'\u00F4' && v != U'\u01A1';
	return true;
}

// valid Vietnamese syllables that are not in the word list (names, rare words):
// onsets and rhymes of known syllables put together, kept when OpenKey's engine accepts them
static std::vector<std::string> rareSyllables() {
	std::vector<std::string> onsets(1, ""), rhymes;
	for (int i = 0; i < kVietnameseSyllablesCount; i++) {
		std::string key = kVietnameseSyllables[i].word;
		size_t n = onsetLength(key);
		std::string o = key.substr(0, n), r = key.substr(n);
		if (std::find(onsets.begin(), onsets.end(), o) == onsets.end()) onsets.push_back(o);
		if (std::find(rhymes.begin(), rhymes.end(), r) == rhymes.end()) rhymes.push_back(r);
	}
	std::vector<std::string> out;
	for (const std::string& o : onsets)
		for (const std::string& r : rhymes) {
			std::string key = o + r;
			if (inWordList(key) || !spelledRight(o, r)) continue;
			std::string keys;
			if (syllableKeys(key, TONE_END, keys)) out.push_back(key);
		}
	return out;
}

struct Tally {
	int words = 0, wordErrors = 0, texts = 0, textErrors = 0;
	std::vector<std::string> examples;
};

static std::vector<std::u32string> splitWords(const std::u32string& s) {
	std::vector<std::u32string> out;
	std::u32string cur;
	for (char32_t c : s) { if (c == U' ') { out.push_back(cur); cur.clear(); } else cur += c; }
	out.push_back(cur);
	return out;
}

static void count(Tally& t, const std::u32string& got, const std::u32string& want, const std::string& keys, FILE* csv,
                  const char* scenario, const char* style, int habit) {
	std::vector<std::u32string> g = splitWords(got), w = splitWords(want);
	t.texts++;
	bool ok = got == want;
	if (!ok) t.textErrors++;
	for (size_t i = 0; i < w.size(); i++) { t.words++; if (i >= g.size() || g[i] != w[i]) t.wordErrors++; }
	if (!ok && t.examples.size() < 6)
		t.examples.push_back(keys + "  ->  " + utf8(got) + "   (expected " + utf8(want) + ")");
	if (csv) {
		std::string k = keys, e = utf8(want), o = utf8(got);
		for (std::string* f : { &k, &e, &o }) { // CSV quoting
			std::string q = "\"";
			for (char c : *f) { if (c == '"') q += '"'; q += c; }
			*f = q + "\"";
		}
		fprintf(csv, "%s,%s,%s,%s,%s,%s,%d\n", scenario, style, habit == Q ? "qwerty" : "dvorak", k.c_str(), e.c_str(), o.c_str(), ok ? 1 : 0);
	}
}

int main(int argc, char** argv) {
	int sentences = argc > 1 ? atoi(argv[1]) : 200;
	unsigned seed = argc > 2 ? (unsigned)atoi(argv[2]) : 2026;
	FILE* csv = argc > 3 ? fopen(argv[3], "w") : nullptr;
	const int wordsPerSentence = 6;
	std::mt19937 rng(seed);

	std::vector<double> weights;
	for (int i = 0; i < kVietnameseSyllablesCount; i++) weights.push_back(std::pow(10.0, kVietnameseSyllables[i].zipf10 / 10.0));
	std::discrete_distribution<int> dist(weights.begin(), weights.end());
	std::vector<std::string> rare = rareSyllables();

	// one sentence of Telex keys; `rareAt` >= 0 puts a rare syllable there
	auto sentence = [&](Style style, int rareAt, bool punctuation, bool capital) {
		std::string keys;
		for (int w = 0; w < wordsPerSentence; w++) {
			std::string k;
			for (int tries = 0; tries < 20; tries++) {
				std::string syl = w == rareAt ? rare[rng() % rare.size()] : kVietnameseSyllables[dist(rng)].word;
				if (syllableKeys(syl, style, k)) break;
				k.clear();
			}
			if (k.empty()) continue;
			if (!keys.empty()) keys += ' ';
			keys += k;
			if (punctuation && w + 1 < wordsPerSentence && rng() % 6 == 0) keys += ',';
		}
		if (punctuation) keys += '.';
		if (capital) keys[0] = (char)toupper(keys[0]);
		return keys + " "; // a space (or enter) after the sentence ends the last word
	};

	enum { RIGHT, WRONG, RARE_RIGHT, RARE_WRONG, TYPO, HANDOVER, LONG_TEXT, SCENARIOS };
	const char* names[SCENARIOS] = {
		"right layout", "wrong layout, after a pause", "rare syllable, right layout", "rare syllable, wrong layout",
		"typo, right layout", "new person, no pause", "5 sentences in a row" };
	Tally tally[SCENARIOS];
	Tally styleTally[STYLE_COUNT][2];

	if (csv) fprintf(csv, "\xEF\xBB\xBF" "scenario,telex_style,habit,keys,expected,on_screen,ok\n");
	for (int s = 0; s < sentences; s++) {
		int habit = s % 2 ? D : Q, other = habit == Q ? D : Q;
		Style style = (Style)(s % STYLE_COUNT);
		bool capital = s % 3 == 0;

		// right / wrong layout, every Telex style
		for (int st = 0; st < STYLE_COUNT; st++) {
			std::string keys = sentence((Style)st, -1, true, capital);
			std::u32string want = engineAlone(keys, habit);
			for (int sc = 0; sc < 2; sc++) {
				Machine m; m.layout = sc == 0 ? habit : other;
				m.type(keys, habit);
				count(tally[sc], m.screen, want, keys, csv, names[sc], kStyleNames[st], habit);
				count(styleTally[st][sc], m.screen, want, keys, nullptr, "", "", habit);
			}
		}
		// a rare syllable somewhere in the sentence
		{
			std::string keys = sentence(style, (int)(rng() % wordsPerSentence), true, capital);
			std::u32string want = engineAlone(keys, habit);
			for (int sc = 0; sc < 2; sc++) {
				Machine m; m.layout = sc == 0 ? habit : other;
				m.type(keys, habit);
				count(tally[RARE_RIGHT + sc], m.screen, want, keys, csv, names[RARE_RIGHT + sc], kStyleNames[style], habit);
			}
		}
		// one wrong letter in the sentence: NhaKey must not make it worse
		{
			std::string keys = sentence(style, -1, true, capital);
			size_t pos;
			do pos = rng() % keys.size(); while (keys[pos] < 'a' || keys[pos] > 'z');
			keys[pos] = "abcdeghiklmnopqtuvy"[rng() % 19];
			std::u32string want = engineAlone(keys, habit);
			Machine m; m.layout = habit;
			m.type("dduwowcj roofi ", habit); // already typing for a while
			size_t from = m.screen.size();
			m.type(keys, habit);
			count(tally[TYPO], m.screen.substr(from), want, keys, csv, names[TYPO], kStyleNames[style], habit);
		}
		// someone else sits down and types right away (no pause)
		{
			std::string first = sentence(style, -1, true, true), keys = sentence(style, -1, true, capital);
			Machine m; m.layout = other;
			m.type(first, other);
			size_t from = m.screen.size();
			m.type(keys, habit);
			count(tally[HANDOVER], m.screen.substr(from), engineAlone(keys, habit), keys, csv, names[HANDOVER], kStyleNames[style], habit);
		}
		// a longer text
		if (s % 5 == 0) {
			std::string keys;
			for (int i = 0; i < 5; i++) keys += sentence(style, i == 2 ? 1 : -1, true, true);
			Machine m; m.layout = habit;
			m.type(keys, habit);
			count(tally[LONG_TEXT], m.screen, engineAlone(keys, habit), keys, csv, names[LONG_TEXT], kStyleNames[style], habit);
		}
	}
	if (csv) fclose(csv);

	// cases people reported: QWERTY Telex typed while Dvorak is on (and the other way round)
	struct Reported { const char* keys; int habit; };
	const Reported reported[] = {
		{ "taij vif ", Q },          // showed "yach vif"
		{ "taij vif ", D },
	};
	int reportedErrors = 0;
	for (const Reported& r : reported) {
		int other = r.habit == Q ? D : Q;
		std::u32string want = engineAlone(r.keys, r.habit);
		for (int afterSomeone = 0; afterSomeone < 2; afterSomeone++) {
			Machine m; m.layout = other;
			if (afterSomeone) m.type("dduwowcj roofi ", other); // someone typed right before, no pause
			size_t from = m.screen.size();
			m.type(r.keys, r.habit);
			if (m.screen.substr(from) != want) {
				reportedErrors++;
				printf("REPORTED CASE FAILS: %s -> %s (expected %s)\n", r.keys, utf8(m.screen.substr(from)).c_str(), utf8(want).c_str());
			}
		}
	}

	printf("%d sentences of %d syllables per scenario (seed %u), %zu rare syllables known to the engine\n\n",
		sentences, wordsPerSentence, seed, rare.size());
	printf("%-30s  %-14s  %s\n", "scenario", "word errors", "texts with an error");
	for (int sc = 0; sc < SCENARIOS; sc++)
		printf("%-30s  %5.2f%% %6s  %5.1f%% (%d/%d)\n", names[sc],
			tally[sc].words ? 100.0 * tally[sc].wordErrors / tally[sc].words : 0.0,
			("(" + std::to_string(tally[sc].wordErrors) + ")").c_str(),
			tally[sc].texts ? 100.0 * tally[sc].textErrors / tally[sc].texts : 0.0, tally[sc].textErrors, tally[sc].texts);
	printf("\nby Telex style (word errors: right layout / wrong layout)\n");
	for (int st = 0; st < STYLE_COUNT; st++)
		printf("  %-32s %5.2f%% / %5.2f%%\n", kStyleNames[st],
			styleTally[st][0].words ? 100.0 * styleTally[st][0].wordErrors / styleTally[st][0].words : 0.0,
			styleTally[st][1].words ? 100.0 * styleTally[st][1].wordErrors / styleTally[st][1].words : 0.0);
	printf("\nreported cases: %s\n", reportedErrors ? "FAIL" : "all right");
	printf("\nexamples\n");
	for (int sc = 0; sc < SCENARIOS; sc++)
		for (const std::string& e : tally[sc].examples) printf("  [%s] %s\n", names[sc], e.c_str());
	return reportedErrors ? 1 : 0;
}
