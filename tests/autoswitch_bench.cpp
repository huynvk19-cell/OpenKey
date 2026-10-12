// Benchmark for AutoSwitch's early layout guess (checkEarly), on samples
// drawn from the built-in word lists, weighted by how common each word is.
// For each sample word it measures, after the first 2 keys, after 3 keys and
// at the end of the word:
//   - wrong layout: typed with QWERTY (or Dvorak) habits while the other
//     layout is active -> NhaKey should switch (higher is better)
//   - right layout: typed on the active layout -> NhaKey must NOT switch
//     (lower is better), both right after a pause and in the middle of typing
//   - typo: right layout, but one letter of the word is wrong (names, slang
//     and typos are not in the word lists) -> must NOT switch either
// Build and run:
//   g++ -std=c++17 -O2 -I Sources/OpenKey/engine tests/autoswitch_bench.cpp Sources/OpenKey/engine/AutoSwitch.cpp -o /tmp/autoswitch_bench
//   /tmp/autoswitch_bench [samples=1000] [seed=2026] [csv file]
#include "AutoSwitch.h"
#include "AutoSwitchData.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

static const int Q = AUTO_LAYOUT_QWERTY, D = AUTO_LAYOUT_DVORAK;

struct Sample { std::string lang, display, telex; };

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

// "<base letters><tone digit>" -> the keys a Telex typist presses ("dduowcj")
static bool telexKeys(const std::string& key, std::string& out) {
	std::u32string base = u32(key.substr(0, key.size() - 1));
	int tone = key.back() - '0';
	out.clear();
	for (size_t i = 0; i < base.size(); i++) {
		char32_t c = base[i];
		if (c == U'ư' && i + 1 < base.size() && base[i + 1] == U'ơ') { out += "uow"; i++; continue; }
		switch (c) {
		case U'ă': out += "aw"; break;
		case U'â': out += "aa"; break;
		case U'đ': out += "dd"; break;
		case U'ê': out += "ee"; break;
		case U'ô': out += "oo"; break;
		case U'ơ': out += "ow"; break;
		case U'ư': out += "uw"; break;
		default: if (c >= 'a' && c <= 'z') out += (char)c; else return false;
		}
	}
	if (tone) out += " sfrxj"[tone];
	// must read back as the same syllable
	std::u32string vn; std::string back;
	return autoSwitchTelex(u32(out), vn, back) && back == key;
}

static std::vector<unsigned int> keysFor(const std::string& text, int layout) {
	std::vector<unsigned int> keys;
	for (char ch : text)
		for (unsigned int sc = 0; sc < 0x60; sc++)
			if (autoSwitchCharFor(sc, false, false, layout) == (char32_t)(unsigned char)ch) { keys.push_back(sc); break; }
	return keys;
}

static std::vector<Sample> pick(const AutoSwitchWord* table, int count, int n, bool vietnamese, std::mt19937& rng) {
	std::vector<double> weights;
	for (int i = 0; i < count; i++) weights.push_back(std::pow(10.0, table[i].zipf10 / 10.0));
	std::discrete_distribution<int> dist(weights.begin(), weights.end());
	std::vector<Sample> out;
	while ((int)out.size() < n) {
		const AutoSwitchWord& w = table[dist(rng)];
		Sample s;
		s.lang = vietnamese ? "vi" : "en";
		if (vietnamese) {
			if (!telexKeys(w.word, s.telex)) continue;
			std::u32string vn; std::string key;
			autoSwitchTelex(u32(s.telex), vn, key);
			s.display = autoSwitchToUtf8(vn);
		} else {
			s.telex = s.display = w.word;
		}
		if (s.telex.size() < 2) continue; // one key words are never guessed early
		out.push_back(s);
	}
	return out;
}

// first key count at which checkEarly asks to switch (0 = never during the word)
static int earlySwitchAt(AutoSwitch& as, const std::vector<unsigned int>& keys, int active, int maxKeys) {
	for (size_t i = 0; i < keys.size() && (int)i < maxKeys; i++) {
		as.pushKey(keys[i], false, false);
		if (as.checkEarly(active) != AUTO_LAYOUT_UNKNOWN) return (int)i + 1;
	}
	return 0;
}

struct Outcome { int at; bool atEnd; }; // early switch key count, switched by word end

static Outcome run(const std::vector<unsigned int>& keys, int active, bool confident) {
	AutoSwitch as;
	if (confident) { // the person has been typing correctly on the active layout
		for (const char* w : { "the", "and" }) {
			for (unsigned int k : keysFor(w, active)) as.pushKey(k, false, false);
			as.endWord(active, true);
		}
	}
	Outcome o;
	o.at = earlySwitchAt(as, keys, active, (int)keys.size());
	if (o.at) { o.atEnd = true; return o; }
	o.atEnd = as.endWord(active, true).switchLayout;
	return o;
}

int main(int argc, char** argv) {
	int n = argc > 1 ? atoi(argv[1]) : 1000;
	unsigned seed = argc > 2 ? (unsigned)atoi(argv[2]) : 2026;
	const char* csvPath = argc > 3 ? argv[3] : nullptr;
	std::mt19937 rng(seed);
	std::vector<Sample> samples = pick(kEnglishWords, kEnglishWordsCount, n / 2, false, rng);
	std::vector<Sample> vi = pick(kVietnameseSyllables, kVietnameseSyllablesCount, n - n / 2, true, rng);
	samples.insert(samples.end(), vi.begin(), vi.end());

	FILE* csv = csvPath ? fopen(csvPath, "w") : nullptr;
	if (csv) fprintf(csv, "\xEF\xBB\xBF"); // UTF-8 mark so Excel shows Vietnamese correctly
	if (csv) fprintf(csv, "id,lang,word,keys_typed,habit,shows_on_wrong_layout,wrong_layout_switch_by_2,wrong_layout_switch_by_3,wrong_layout_switch_by_end,right_layout_false_by_2,right_layout_false_by_3,right_layout_false_by_end,midtyping_false_by_2,midtyping_false_by_3,midtyping_false_by_end,typo_word,typo_false_by_2,typo_false_by_3,typo_false_by_end\n");

	// [lang 0=en 1=vi 2=all][metric]
	enum { W2, W3, WE, R2, R3, RE, C2, C3, CE, T2, T3, TE, N };
	int tally[3][N] = {};
	int total[3] = {};
	for (size_t i = 0; i < samples.size(); i++) {
		const Sample& s = samples[i];
		int habit = i % 2 ? D : Q; // half QWERTY people, half Dvorak people
		int other = habit == Q ? D : Q;
		std::vector<unsigned int> keys = keysFor(s.telex, habit);
		Outcome wrong = run(keys, other, false);
		Outcome right = run(keys, habit, false);
		Outcome mid = run(keys, habit, true);
		std::string typo = s.telex;
		typo[rng() % typo.size()] = "abcdefghijklmnopqrstuvwxyz"[rng() % 26];
		Outcome typoRun = run(keysFor(typo, habit), habit, true);
		bool v[N] = {
			wrong.at && wrong.at <= 2, wrong.at && wrong.at <= 3, wrong.atEnd,
			right.at && right.at <= 2, right.at && right.at <= 3, right.atEnd,
			mid.at && mid.at <= 2, mid.at && mid.at <= 3, mid.atEnd,
			typoRun.at && typoRun.at <= 2, typoRun.at && typoRun.at <= 3, typoRun.atEnd };
		int lang = s.lang == "vi" ? 1 : 0;
		for (int l : { lang, 2 }) { total[l]++; for (int m = 0; m < N; m++) tally[l][m] += v[m]; }
		if (csv) {
			std::u32string shown;
			for (unsigned int k : keys) shown += autoSwitchCharFor(k, false, false, other);
			std::string shownText = autoSwitchToUtf8(shown);
			if (shownText.find_first_of(",\"") != std::string::npos) { // CSV quoting
				std::string quoted = "\"";
				for (char c : shownText) { if (c == '"') quoted += '"'; quoted += c; }
				shownText = quoted + "\"";
			}
			fprintf(csv, "%zu,%s,%s,%s,%s,%s", i + 1, s.lang.c_str(), s.display.c_str(), s.telex.c_str(),
				habit == Q ? "qwerty" : "dvorak", shownText.c_str());
			for (int m = 0; m < N; m++) {
				if (m == T2) fprintf(csv, ",%s", typo.c_str());
				fprintf(csv, ",%d", v[m] ? 1 : 0);
			}
			fprintf(csv, "\n");
		}
	}
	if (csv) fclose(csv);

	auto pct = [](int a, int b) { return b ? 100.0 * a / b : 0.0; };
	const char* names[3] = { "English", "Vietnamese", "All" };
	printf("%d samples (seed %u)\n\n", (int)samples.size(), seed);
	const char* groups[4] = { "wrong layout: switched (higher is better)", "right layout, after a pause: false switch",
		"right layout, mid-typing: false switch", "right layout with a typo: false switch" };
	for (int g = 0; g < 4; g++) {
		printf("%s\n%-11s   by 2     by 3     by end\n", groups[g], "");
		for (int l = 0; l < 3; l++)
			printf("%-11s  %5.1f%%   %5.1f%%   %5.1f%%\n", names[l],
				pct(tally[l][g * 3], total[l]), pct(tally[l][g * 3 + 1], total[l]), pct(tally[l][g * 3 + 2], total[l]));
		printf("\n");
	}
	return 0;
}
