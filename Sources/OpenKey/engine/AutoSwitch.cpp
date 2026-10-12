/*----------------------------------------------------------
OpenKey - The Cross platform Open source Vietnamese Keyboard application.

AutoSwitch: per-word layout (QWERTY / Dvorak) and language
(English / Vietnamese Telex) detection. See AutoSwitch.h.

Released under GPL license, like the rest of OpenKey.
-----------------------------------------------------------*/
#include "AutoSwitch.h"
#include "AutoSwitchData.h"

#include <cstring>
#include <set>

// Minimum score (zipf * 10) for the other layout's reading to be trusted.
#define AUTO_SWITCH_MIN_OTHER_SCORE 30
// Bonus given to the language of the recent words when a word is valid in both.
#define AUTO_SWITCH_CONTEXT_BONUS 15
#define AUTO_SWITCH_CONTEXT_MAX 3
// In Vietnamese mode English readings count this much less (zipf * 10).
#define AUTO_SWITCH_ENGLISH_PENALTY 15
// A real Vietnamese syllable that is not in the word list (rare word, name).
#define AUTO_SWITCH_RARE_VIETNAMESE 20
// Mid-typing, a word that is no word switches only to a word at least this common.
#define AUTO_SWITCH_TYPO_SCORE 50

// ---------------------------------------------------------------- layouts
// Scan code set 1, rows: number row 0x02-0x0D, top 0x10-0x1B,
// home 0x1E-0x28, bottom 0x2C-0x35, plus 0x29 (`) and 0x2B (\).
struct KeyRow { unsigned int first; const char* qwerty; const char* qwertyShift; const char* dvorak; const char* dvorakShift; };

static const KeyRow kRows[] = {
	{ 0x02, "1234567890-=", "!@#$%^&*()_+", "1234567890[]", "!@#$%^&*(){}" },
	{ 0x10, "qwertyuiop[]", "QWERTYUIOP{}", "',.pyfgcrl/=", "\"<>PYFGCRL?+" },
	{ 0x1E, "asdfghjkl;'`", "ASDFGHJKL:\"~", "aoeuidhtns-`", "AOEUIDHTNS_~" },
	{ 0x2B, "\\zxcvbnm,./", "|ZXCVBNM<>?", "\\;qjkxbmwvz", "|:QJKXBMWVZ" },
};

static const KeyRow* findRow(unsigned int scanCode, unsigned int& index) {
	for (const KeyRow& row : kRows) {
		size_t len = strlen(row.qwerty);
		if (scanCode >= row.first && scanCode < row.first + len) {
			index = scanCode - row.first;
			return &row;
		}
	}
	return nullptr;
}

bool autoSwitchIsTrackedKey(unsigned int scanCode) {
	unsigned int index;
	return findRow(scanCode, index) != nullptr;
}

char32_t autoSwitchCharFor(unsigned int scanCode, bool shift, bool capsLock, int layout) {
	unsigned int index;
	const KeyRow* row = findRow(scanCode, index);
	if (!row) return 0;
	bool dvorak = layout == AUTO_LAYOUT_DVORAK;
	char32_t base = (unsigned char)(dvorak ? row->dvorak : row->qwerty)[index];
	char32_t shifted = (unsigned char)(dvorak ? row->dvorakShift : row->qwertyShift)[index];
	// Caps lock only affects letters, and which keys are letters depends on the layout.
	bool isLetter = base >= 'a' && base <= 'z';
	return (isLetter ? (shift != capsLock) : shift) ? shifted : base;
}

// ---------------------------------------------------------------- unicode helpers
std::string autoSwitchToUtf8(const std::u32string& s) {
	std::string out;
	for (char32_t c : s) {
		if (c < 0x80) out += (char)c;
		else if (c < 0x800) { out += (char)(0xC0 | (c >> 6)); out += (char)(0x80 | (c & 0x3F)); }
		else if (c < 0x10000) { out += (char)(0xE0 | (c >> 12)); out += (char)(0x80 | ((c >> 6) & 0x3F)); out += (char)(0x80 | (c & 0x3F)); }
		else { out += (char)(0xF0 | (c >> 18)); out += (char)(0x80 | ((c >> 12) & 0x3F)); out += (char)(0x80 | ((c >> 6) & 0x3F)); out += (char)(0x80 | (c & 0x3F)); }
	}
	return out;
}

static bool isAsciiLetter(char32_t c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static char32_t asciiLower(char32_t c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

static char32_t vnUpper(char32_t c) {
	if (c >= 'a' && c <= 'z') return c - 32;
	if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return c - 32;
	if (c >= 0x100 && c <= 0x17F) return (c & 1) ? c - 1 : c;            // a-breve, d-stroke, i/u-tilde ...
	if (c == 0x1A1 || c == 0x1B0) return c - 1;                            // o-horn, u-horn
	if (c >= 0x1EA0 && c <= 0x1EF9) return (c & 1) ? c - 1 : c;          // Latin Extended Additional: toned vowels
	return c;
}

// ---------------------------------------------------------------- Vietnamese
static const char32_t kVowels[] = U"a\u0103\u00E2e\u00EAio\u00F4\u01A1u\u01B0y";
// Each row: base vowel with tone 0..5 (none, sac, huyen, hoi, nga, nang).
static const char32_t* kToneTable[] = {
	U"a\u00E1\u00E0\u1EA3\u00E3\u1EA1", U"\u0103\u1EAF\u1EB1\u1EB3\u1EB5\u1EB7", U"\u00E2\u1EA5\u1EA7\u1EA9\u1EAB\u1EAD", U"e\u00E9\u00E8\u1EBB\u1EBD\u1EB9", U"\u00EA\u1EBF\u1EC1\u1EC3\u1EC5\u1EC7", U"i\u00ED\u00EC\u1EC9\u0129\u1ECB",
	U"o\u00F3\u00F2\u1ECF\u00F5\u1ECD", U"\u00F4\u1ED1\u1ED3\u1ED5\u1ED7\u1ED9", U"\u01A1\u1EDB\u1EDD\u1EDF\u1EE1\u1EE3", U"u\u00FA\u00F9\u1EE7\u0169\u1EE5", U"\u01B0\u1EE9\u1EEB\u1EED\u1EEF\u1EF1", U"y\u00FD\u1EF3\u1EF7\u1EF9\u1EF5",
};

static int vowelIndex(char32_t c) {
	for (int i = 0; kVowels[i]; i++) if (kVowels[i] == c) return i;
	return -1;
}
static bool isVowel(char32_t c) { return vowelIndex(c) >= 0; }
static bool isModifiedVowel(char32_t c) { return c == U'\u0103' || c == U'\u00E2' || c == U'\u00EA' || c == U'\u00F4' || c == U'\u01A1' || c == U'\u01B0'; }

static int toneOfKey(char32_t c) {
	switch (c) {
		case 's': return 1; case 'f': return 2; case 'r': return 3; case 'x': return 4; case 'j': return 5;
	}
	return 0;
}

static char32_t circumflexOf(char32_t c) {
	switch (c) { case 'a': case U'\u0103': return U'\u00E2'; case 'e': return U'\u00EA'; case 'o': case U'\u01A1': return U'\u00F4'; }
	return 0;
}
static char32_t plainOf(char32_t c) {
	switch (c) {
		case U'\u0103': case U'\u00E2': return 'a'; case U'\u00EA': return 'e';
		case U'\u00F4': case U'\u01A1': return 'o'; case U'\u01B0': return 'u'; case U'\u0111': return 'd';
	}
	return c;
}
static char32_t hornOf(char32_t c) {
	switch (c) { case 'a': case U'\u00E2': return U'\u0103'; case 'o': case U'\u00F4': return U'\u01A1'; case 'u': return U'\u01B0'; }
	return 0;
}

static bool _modernOrthography = false;
void autoSwitchSetModernOrthography(bool modern) { _modernOrthography = modern; }

// Index (in letters) of the vowel that carries the tone. -1 if none.
static int tonePosition(const std::u32string& letters) {
	std::vector<int> nucleus;
	for (int i = 0; i < (int)letters.size(); i++) {
		if (!isVowel(letters[i])) continue;
		bool nextIsVowel = i + 1 < (int)letters.size() && isVowel(letters[i + 1]);
		// "qu" and "gi" (before another vowel) belong to the initial consonant.
		if (i == 1 && letters[i] == 'u' && letters[0] == 'q') continue;
		if (i == 1 && letters[i] == 'i' && letters[0] == 'g' && nextIsVowel) continue;
		nucleus.push_back(i);
	}
	if (nucleus.empty()) return -1;
	int chosen = -1;
	for (int i : nucleus) {
		if (isModifiedVowel(letters[i])) {
			if (chosen >= 0 && letters[chosen] == U'\u01B0' && letters[i] == U'\u01A1') chosen = i;
			else if (chosen < 0 || letters[chosen] != U'\u01A1') chosen = i;
		}
	}
	if (chosen >= 0) return chosen;
	bool hasFinal = nucleus.back() < (int)letters.size() - 1;
	if (hasFinal || nucleus.size() == 1) return nucleus.back() == nucleus.front() ? nucleus.front() : nucleus.back();
	if (nucleus.size() == 2) {
		char32_t a = letters[nucleus[0]], b = letters[nucleus[1]];
		if (_modernOrthography && ((a == 'o' && (b == 'a' || b == 'e')) || (a == 'u' && b == 'y'))) return nucleus[1]; // "hoa" + huyen -> tone on the a
		return nucleus[0];
	}
	return nucleus[1];
}

bool autoSwitchTelex(const std::u32string& raw, std::u32string& vietnamese, std::string& key) {
	std::u32string letters;
	std::vector<bool> upper;
	int tone = 0;
	bool escaped = false;

	auto hasVowel = [&]() { for (char32_t c : letters) if (isVowel(c)) return true; return false; };
	auto append = [&](char32_t c, bool up) { letters += c; upper.push_back(up); };

	for (char32_t rc : raw) {
		if (!isAsciiLetter(rc)) return false;
		bool up = rc >= 'A' && rc <= 'Z';
		char32_t c = asciiLower(rc);

		int t = toneOfKey(c);
		if (t && hasVowel()) {
			if (tone == t) { tone = 0; append(c, up); escaped = true; }
			else tone = t;
			continue;
		}
		if (c == 'z' && hasVowel() && tone) { tone = 0; continue; }
		if (c == 'd' && !letters.empty() && (letters[0] == 'd' || letters[0] == U'\u0111')) {
			if (letters[0] == 'd') letters[0] = U'\u0111';
			else { letters[0] = 'd'; append(c, up); escaped = true; }
			continue;
		}
		if (c == 'a' || c == 'e' || c == 'o') {
			bool handled = false;
			for (int i = (int)letters.size() - 1; i >= 0; i--) {
				char32_t l = letters[i];
				if (plainOf(l) != c || !isVowel(l)) continue;
				if (l == circumflexOf(c)) { letters[i] = c; append(c, up); escaped = true; }
				else letters[i] = circumflexOf(c);
				handled = true;
				break;
			}
			if (handled) continue;
		}
		if (c == 'w') {
			// "uo" + w -> u-horn o-horn
			bool handled = false;
			for (int i = (int)letters.size() - 2; i >= 0; i--) {
				if (plainOf(letters[i]) == 'u' && plainOf(letters[i + 1]) == 'o') {
					if (letters[i] == U'\u01B0' && letters[i + 1] == U'\u01A1') { letters[i] = 'u'; letters[i + 1] = 'o'; append(c, up); escaped = true; }
					else { letters[i] = U'\u01B0'; letters[i + 1] = U'\u01A1'; }
					handled = true;
					break;
				}
			}
			if (!handled) {
				for (int i = (int)letters.size() - 1; i >= 0; i--) {
					char32_t l = letters[i];
					if (!isVowel(l) || !hornOf(plainOf(l)) || plainOf(l) == 'e') continue;
					if (l == hornOf(plainOf(l))) { letters[i] = plainOf(l); append(c, up); escaped = true; }
					else letters[i] = hornOf(l);
					handled = true;
					break;
				}
			}
			if (!handled) append(U'\u01B0', up);
			continue;
		}
		append(c, up);
	}
	if (escaped) return false;

	key = autoSwitchToUtf8(letters) + (char)('0' + tone);

	int pos = tonePosition(letters);
	if (pos < 0 && tone) return false;
	vietnamese.clear();
	for (int i = 0; i < (int)letters.size(); i++) {
		char32_t c = letters[i];
		if (i == pos && tone) c = kToneTable[vowelIndex(c)][tone];
		vietnamese += upper[i] ? vnUpper(c) : c;
	}
	return true;
}

// ---------------------------------------------------------------- dictionaries
static int lookup(const AutoSwitchWord* table, int count, const std::string& word) {
	int lo = 0, hi = count - 1;
	while (lo <= hi) {
		int mid = (lo + hi) / 2;
		int cmp = strcmp(table[mid].word, word.c_str());
		if (cmp == 0) return table[mid].zipf10;
		if (cmp < 0) lo = mid + 1; else hi = mid - 1;
	}
	return 0;
}

int autoSwitchEnglishScore(const std::u32string& word) {
	std::string lower;
	for (char32_t c : word) {
		if (!isAsciiLetter(c)) return 0;
		lower += (char)asciiLower(c);
	}
	return lookup(kEnglishWords, kEnglishWordsCount, lower);
}

// ---------------------------------------------------------------- Vietnamese syllable structure
// The word list only has the common syllables. Names and rare words are still
// real Vietnamese: a syllable is valid when it is an initial consonant plus a
// rhyme (with its tone) that Vietnamese uses, spelled the Vietnamese way.
static const char* kVnOnsets[] = { "ngh", "ng", "nh", "ch", "gh", "gi", "kh", "ph", "qu", "th", "tr",
	"b", "c", "d", "\xC4\x91", "g", "h", "k", "l", "m", "n", "p", "r", "s", "t", "v", "x" };

static char32_t firstChar(const std::string& utf8) {
	const unsigned char* p = (const unsigned char*)utf8.c_str();
	if (*p < 0x80) return *p;
	if ((*p & 0xE0) == 0xC0) return ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
	return ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
}

// c/k, g/gh, ng/ngh, qu: which letters may follow
static bool vnSpellingOk(const std::string& onset, char32_t vowel) {
	bool front = vowel == 'e' || vowel == U'\u00EA' || vowel == 'i' || vowel == 'y';
	bool frontE = vowel == 'e' || vowel == U'\u00EA' || vowel == 'i';
	if (onset == "c") return !front;
	if (onset == "k") return front;
	if (onset == "g") return vowel != 'e' && vowel != U'\u00EA';
	if (onset == "gh" || onset == "ngh") return frontE;
	if (onset == "ng") return !frontE;
	if (onset == "gi") return vowel != 'i';
	if (onset == "qu") return vowel != 'u' && vowel != U'\u01B0' && vowel != 'o' && vowel != U'\u00F4' && vowel != U'\u01A1';
	return true;
}

struct VnStructure {
	std::set<std::string> rhymes;       // "anh1": rhyme with marks and tone digit
	std::set<std::string> plainPrefixes; // every prefix of a rhyme without marks ("u", "uy", "uye", "uyen")
	VnStructure() {
		for (int i = 0; i < kVietnameseSyllablesCount; i++) {
			std::string key = kVietnameseSyllables[i].word;
			for (const char* o : kVnOnsets) {
				size_t n = strlen(o);
				if (key.compare(0, n, o) != 0 || !isVowel(firstChar(key.substr(n)))) continue;
				addRhyme(key.substr(n));
				break;
			}
			if (isVowel(firstChar(key))) addRhyme(key);
		}
	}
	void addRhyme(const std::string& rhyme) {
		rhymes.insert(rhyme);
		std::u32string letters = u32FromUtf8(rhyme.substr(0, rhyme.size() - 1));
		std::string plain;
		for (char32_t c : letters) plain += (char)plainOf(c);
		for (size_t n = 1; n <= plain.size(); n++) plainPrefixes.insert(plain.substr(0, n));
	}
	static std::u32string u32FromUtf8(const std::string& s) {
		std::u32string out;
		for (size_t i = 0; i < s.size();) {
			unsigned char c = s[i];
			if (c < 0x80) { out += c; i++; }
			else if ((c & 0xE0) == 0xC0) { out += ((c & 0x1F) << 6) | (s[i + 1] & 0x3F); i += 2; }
			else { out += ((c & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F); i += 3; }
		}
		return out;
	}
};

static const VnStructure& vnStructure() {
	static VnStructure structure;
	return structure;
}

// "<letters><tone digit>" is a Vietnamese syllable, even if it is not in the word list
static bool vnSyllableValid(const std::string& key) {
	const VnStructure& vs = vnStructure();
	if (vs.rhymes.count(key) && isVowel(firstChar(key))) return true;
	for (const char* o : kVnOnsets) {
		size_t n = strlen(o);
		if (key.compare(0, n, o) != 0) continue;
		std::string rhyme = key.substr(n);
		if (vs.rhymes.count(rhyme) && vnSpellingOk(o, firstChar(rhyme))) return true;
	}
	return false;
}

// plain letters ("nghuy") can still grow into a Vietnamese syllable
static bool vnPlainPrefixValid(const std::string& plain) {
	const VnStructure& vs = vnStructure();
	if (vs.plainPrefixes.count(plain)) return true;
	for (const char* o : kVnOnsets) {
		std::string onset = o;
		if (onset == "\xC4\x91") continue; // plain letters have d for d-stroke
		if (plain.size() <= onset.size()) {
			if (onset.compare(0, plain.size(), plain) == 0) return true; // still typing the consonant
			continue;
		}
		if (plain.compare(0, onset.size(), onset) != 0) continue;
		std::string rest = plain.substr(onset.size());
		if (vs.plainPrefixes.count(rest) && vnSpellingOk(onset, (char32_t)rest[0])) return true;
	}
	return false;
}

int autoSwitchVietnameseScore(const std::string& key) {
	int score = lookup(kVietnameseSyllables, kVietnameseSyllablesCount, key);
	if (score == 0 && vnSyllableValid(key)) score = AUTO_SWITCH_RARE_VIETNAMESE;
	return score;
}

static bool startsWith(const char* word, const std::string& prefix) {
	return strncmp(word, prefix.c_str(), prefix.size()) == 0;
}

// Highest score (zipf * 10) of any word starting with `prefix`, 0 if none.
static int prefixScore(const AutoSwitchWord* table, int count, const std::string& prefix) {
	int lo = 0, hi = count;
	while (lo < hi) {
		int mid = (lo + hi) / 2;
		if (strcmp(table[mid].word, prefix.c_str()) < 0) lo = mid + 1; else hi = mid;
	}
	int best = 0;
	for (int i = lo; i < count && startsWith(table[i].word, prefix); i++)
		if (table[i].zipf10 > best) best = table[i].zipf10;
	return best;
}

int autoSwitchPrefixScore(const std::u32string& letters) {
	std::string lower;
	for (char32_t c : letters) {
		if (!isAsciiLetter(c)) return 0;
		lower += (char)asciiLower(c);
	}
	int best = prefixScore(kEnglishWords, kEnglishWordsCount, lower);
	std::u32string vietnamese;
	std::string key;
	if (!autoSwitchTelex(letters, vietnamese, key)) return best;
	std::string plain;
	for (char32_t c : vietnamese) {
		char32_t p = c;
		for (int v = 0; kVowels[v]; v++)
			for (int t = 0; t < 6; t++)
				if (kToneTable[v][t] == c || vnUpper(kToneTable[v][t]) == c) p = kVowels[v];
		p = plainOf(p);
		if (p == U'\u0110') p = 'd';
		if (!isAsciiLetter(p)) return best;
		plain += (char)asciiLower(p);
	}
	int vn = prefixScore(kVietnamesePlain, kVietnamesePlainCount, plain);
	if (vn == 0 && vnPlainPrefixValid(plain)) vn = AUTO_SWITCH_RARE_VIETNAMESE;
	return vn > best ? vn : best;
}

bool autoSwitchCouldBeWord(const std::u32string& letters) {
	return autoSwitchPrefixScore(letters) > 0;
}

// ---------------------------------------------------------------- decision
struct AutoSwitchReading {
	std::u32string all, prefix, core, suffix;
	bool clean = false;     // core is letters only
	int english = 0;
	int vietnamese = 0;     // 0 when not valid Telex Vietnamese
	std::u32string vietnameseText;
	bool vietnameseChanged = false; // Telex changed the letters (marks/tones)

	int score() const { return clean ? (english > vietnamese ? english : vietnamese) : 0; }
};
typedef AutoSwitchReading Reading;

template <class Keys>
static Reading readAs(const Keys& keys, int layout) {
	Reading r;
	for (const auto& k : keys) r.all += autoSwitchCharFor(k.scanCode, k.shift, k.capsLock, layout);
	size_t start = 0, end = r.all.size();
	while (start < end && !isAsciiLetter(r.all[start])) start++;
	while (end > start && !isAsciiLetter(r.all[end - 1])) end--;
	r.prefix = r.all.substr(0, start);
	r.core = r.all.substr(start, end - start);
	r.suffix = r.all.substr(end);
	r.clean = !r.core.empty();
	for (char32_t c : r.core) if (!isAsciiLetter(c)) r.clean = false;
	if (!r.clean) return r;
	r.english = autoSwitchEnglishScore(r.core);
	std::string key;
	if (autoSwitchTelex(r.core, r.vietnameseText, key)) {
		r.vietnamese = autoSwitchVietnameseScore(key);
		r.vietnameseChanged = r.vietnameseText != r.core;
	}
	return r;
}

void AutoSwitch::pushKey(unsigned int scanCode, bool shift, bool capsLock) {
	if (!autoSwitchIsTrackedKey(scanCode)) { _valid = false; return; }
	_keys.push_back({ scanCode, shift, capsLock });
}

void AutoSwitch::invalidate() { _valid = false; _previousLean = 0; _previousKeys.clear(); }

// How good a reading is. Typing Vietnamese comes first: in Vietnamese mode a
// rare English word ("pag", "yr") must not keep the wrong layout when the other
// reading is a real Vietnamese word, so English counts a bit less there.
static int readingScore(const Reading& r, bool vietnameseMode) {
	if (!r.clean) return 0;
	if (!vietnameseMode) return r.score();
	int english = r.english - AUTO_SWITCH_ENGLISH_PENALTY;
	return r.vietnamese > english ? r.vietnamese : (english > 0 ? english : 0);
}

bool AutoSwitch::previousWordFix(int layout, bool vietnameseMode, std::u32string& text) const {
	if (_previousKeys.empty()) return false;
	Reading r = readAs(_previousKeys, layout);
	if (!r.clean || !r.prefix.empty() || r.suffix.size() > 1) return false;
	int score = readingScore(r, vietnameseMode);
	// right after a pause the layout was not confirmed yet: any word will do.
	// Otherwise (someone else sat down without a pause) only a clearly better word.
	if (score <= 0 || (_previousConfirmed && score < _previousScore + AUTO_SWITCH_CONTEXT_BONUS)) return false;
	bool vietnamese;
	text = chooseText(r, vietnameseMode, vietnamese);
	return true;
}

void AutoSwitch::reset() {
	_keys.clear();
	_valid = true;
}

// Could the text typed so far (on one layout) still become a word?
// > 0 = yes (best score of a word it could become), 0 = no, -1 = can not tell yet.
static bool isClosing(char32_t c) {
	// not ';': it is the Dvorak "s" (the sac tone) typed on QWERTY
	return c == ',' || c == '.' || c == ':' || c == '!' || c == '?' || c == ')' || c == ']' ||
		c == '}' || c == '\'' || c == '"' || c == '>';
}

static bool isOpening(char32_t c) {
	return c == '\'' || c == '"' || c == '(' || c == '[' || c == '{' || c == '<' || c == '`';
}

// Score of a finished word (letters only), English or Vietnamese Telex.
static int wordScore(const std::u32string& letters) {
	int best = autoSwitchEnglishScore(letters);
	std::u32string vietnamese;
	std::string key;
	if (autoSwitchTelex(letters, vietnamese, key)) {
		int vn = autoSwitchVietnameseScore(key);
		if (vn > best) best = vn;
	}
	return best;
}

static int couldStillBeWord(const std::u32string& all) {
	if (all.empty()) return -1;
	size_t start = 0;
	if (!isAsciiLetter(all[0])) {
		if (!isOpening(all[0])) return 0;          // a word does not start with , . ; / =
		start = 1;                                  // one opening quote or bracket
	}
	size_t letters = 0, i = start;
	while (i < all.size() && isAsciiLetter(all[i])) { i++; letters++; }
	for (size_t j = i; j < all.size(); j++)
		if (isAsciiLetter(all[j])) return 0;       // punctuation inside a word
	if (all.size() - i >= 2) return 0;              // two or more symbols in a row
	if (letters == 0) return start && all.size() == 1 ? -1 : 0;
	if (i < all.size())                             // "word," : the word is already finished
		return wordScore(all.substr(start, letters));
	return autoSwitchPrefixScore(all.substr(start, letters));
}

int AutoSwitch::checkEarly(int currentLayout) const {
	if (!_valid || _keys.size() < 2 || currentLayout == AUTO_LAYOUT_UNKNOWN) return AUTO_LAYOUT_UNKNOWN;
	int otherLayout = currentLayout == AUTO_LAYOUT_QWERTY ? AUTO_LAYOUT_DVORAK : AUTO_LAYOUT_QWERTY;
	std::u32string current, other;
	for (const Key& k : _keys) {
		current += autoSwitchCharFor(k.scanCode, k.shift, k.capsLock, currentLayout);
		other += autoSwitchCharFor(k.scanCode, k.shift, k.capsLock, otherLayout);
	}
	int currentScore = couldStillBeWord(current);
	if (currentScore < 0) return AUTO_LAYOUT_UNKNOWN;
	// the other reading must be plain letters that can still become a word
	size_t letters = 0;
	for (char32_t c : other) { if (!isAsciiLetter(c)) return AUTO_LAYOUT_UNKNOWN; letters++; }
	if (letters < 2) return AUTO_LAYOUT_UNKNOWN;
	int otherScore = autoSwitchPrefixScore(other);
	if (otherScore == 0) return AUTO_LAYOUT_UNKNOWN;
	// switch when the current reading is impossible, or only leads to rare
	// words while the other one leads to common words
	int strong = _confirmedWords > 0 ? _earlyStrong : _earlyStrongFirst;
	int margin = _confirmedWords > 0 ? _earlyMargin : _earlyMarginFirst;
	// in the middle of typing on this layout, 2 keys are too few to tell a typo from a new person
	if (_confirmedWords > 0 && _keys.size() < 3) return AUTO_LAYOUT_UNKNOWN;
	if (currentScore == 0) {
		// right after a pause (maybe someone else sat down): switch at once.
		// In the middle of typing on this layout it is more likely a typo or
		// a name: wait for a third key and a common word on the other layout.
		if (_confirmedWords == 0 || otherScore >= strong) return otherLayout;
		return AUTO_LAYOUT_UNKNOWN;
	}
	if (otherScore >= strong && otherScore - currentScore >= margin) return otherLayout;
	return AUTO_LAYOUT_UNKNOWN;
}

// The word as it should appear for one reading (English or Vietnamese).
std::u32string AutoSwitch::chooseText(const Reading& r, bool vietnameseMode, bool& vietnamese) const {
	// Vietnamese mode: a valid Vietnamese word always stays Vietnamese ("as" is a-acute).
	// English words that are not Vietnamese are restored by OpenKey's own spell check.
	vietnamese = r.clean && r.vietnamese > 0 && vietnameseMode;
	return r.clean ? r.prefix + (vietnamese ? r.vietnameseText : r.core) + r.suffix : r.all;
}

AutoSwitchResult AutoSwitch::endWord(int currentLayout, bool vietnameseMode, bool allowLayoutSwitch) {
	AutoSwitchResult result;
	int earlyFrom = _earlyFrom;
	_earlyFrom = AUTO_LAYOUT_UNKNOWN;
	if (!_valid || _keys.empty() || currentLayout == AUTO_LAYOUT_UNKNOWN) {
		_previousLean = 0;
		_previousKeys.clear();
		reset();
		return result;
	}
	int otherLayout = currentLayout == AUTO_LAYOUT_QWERTY ? AUTO_LAYOUT_DVORAK : AUTO_LAYOUT_QWERTY;
	Reading current = readAs(_keys, currentLayout);
	Reading other = readAs(_keys, otherLayout);

	// At the end of a word, switch when the other reading is a real word made
	// only of letters and the current one is not a word, has stray symbols
	// (",ao" for "was"), or is a much rarer word ("yr" for "to").
	bool otherPlain = other.clean && other.prefix.empty() && other.core.size() >= 2 &&
		(other.suffix.empty() || (other.suffix.size() == 1 && isClosing(other.suffix[0])));
	// "nam," "(ok" are normal: one closing punctuation mark or one opening quote / bracket
	bool currentHasSymbols = !current.clean || current.prefix.size() > 1 || current.suffix.size() > 1 ||
		(!current.prefix.empty() && !isOpening(current.prefix[0])) ||
		(!current.suffix.empty() && !isClosing(current.suffix[0]));
	int currentScore = currentHasSymbols ? 0 : readingScore(current, vietnameseMode);
	int otherScore = readingScore(other, vietnameseMode);
	// English counts less only against a real word: next to gibberish it counts in full
	if (currentScore == 0 && other.clean && other.score() > otherScore) otherScore = other.score();
	int lean = (allowLayoutSwitch && otherPlain && otherScore >= AUTO_SWITCH_MIN_OTHER_SCORE)
	           ? otherScore - currentScore : 0;
	// Typing on this layout for a while: a word that is not a word is more likely
	// a typo or a name than someone else typing, so ask for a common word.
	int needed = _confirmedWords > 0 && current.clean ? AUTO_SWITCH_TYPO_SCORE : AUTO_SWITCH_MIN_OTHER_SCORE;
	bool doSwitch = lean > 0 && (currentScore == 0 ? otherScore >= needed : (otherScore >= _endStrong && lean >= _endMargin));
	// The switch in the middle of this word made it no word at all (a typo or a
	// name on the right layout): go back and retype what was typed.
	bool revert = !doSwitch && allowLayoutSwitch && earlyFrom == otherLayout && readingScore(current, vietnameseMode) == 0;
	if (revert) doSwitch = true;
	// Two words in a row that both read better on the other layout ("an all"):
	// switch and fix both words.
	bool fixPrevious = false;
	if (!doSwitch && !revert && lean > 0 && _previousLean > 0 && lean + _previousLean >= _pairMargin) {
		doSwitch = true;
		fixPrevious = true;
	}

	const Reading& r = doSwitch ? other : current;
	bool layoutWasConfirmed = _confirmedWords > 0;
	if (doSwitch && !revert) _languageContext = 0; // probably a different person typing now
	if (doSwitch && !revert) _confirmedWords = 0;
	else if (currentScore >= AUTO_SWITCH_MIN_OTHER_SCORE && _confirmedWords < 100) _confirmedWords++;

	bool vietnamese = false;
	result.text = chooseText(r, vietnameseMode, vietnamese);
	result.switchLayout = doSwitch;
	result.layout = doSwitch ? otherLayout : currentLayout;
	result.vietnamese = vietnamese;
	// OpenKey keeps a valid Vietnamese word on screen, so an English choice
	// for such a word needs a retype. A layout switch always needs one.
	result.replace = doSwitch || (vietnameseMode && r.clean && r.vietnamese > 0 && r.vietnameseChanged && !vietnamese);
	if (fixPrevious) {
		result.replacePrevious = true;
		result.previousText = _previousOtherText;
	} else if (doSwitch && !revert && previousWordFix(otherLayout, vietnameseMode, result.previousText)) {
		result.replacePrevious = true; // the word before was typed on the new layout too
	}
	// keep this word: a switch on the next word may fix it too
	_previousKeys.clear();
	if (!doSwitch) {
		for (const Key& k : _keys) _previousKeys.push_back(k);
		_previousConfirmed = layoutWasConfirmed;
		_previousScore = currentScore;
	}

	// remember this word in case the next one also leans to the other layout
	if (!doSwitch && lean > 0) {
		bool otherVietnamese;
		_previousLean = lean;
		_previousOtherText = chooseText(other, vietnameseMode, otherVietnamese);
	} else {
		_previousLean = 0;
	}

	if (r.clean && !(r.vietnamese > 0 && !r.vietnameseChanged)) {
		if (vietnamese) { if (_languageContext < AUTO_SWITCH_CONTEXT_MAX) _languageContext++; }
		else if (r.english > 0) { if (_languageContext > -AUTO_SWITCH_CONTEXT_MAX) _languageContext--; }
	}
	reset();
	return result;
}
