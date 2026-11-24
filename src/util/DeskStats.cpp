#include "DeskStats.h"

#include <HalClock.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <new>
#include <vector>

#include "Dictionary.h"
#include "DictionaryRegistry.h"

namespace desk_stats {
namespace {

constexpr char DAYS_PATH[] = "/.crosspoint/desk_days.txt";
constexpr char LOOKUPS_PATH[] = "/.crosspoint/desk_words.txt";
constexpr char WOTD_PATH[] = "/.crosspoint/desk_wotd4.txt";
// A few years of "YYYYMM HHHHHHHH\n" lines; older months are dropped on write.
constexpr int MAX_DAY_LINES = 36;
constexpr size_t DAYS_BUF = MAX_DAY_LINES * 16 + 1;
constexpr size_t LOOKUPS_CAP = MAX_LOOKUPS * MAX_WORD_BYTES + 16;
constexpr size_t MEANING_MAX_BYTES = 90;
constexpr size_t IPA_MAX_BYTES = 48;

struct DayLine {
  uint32_t yyyymm;
  uint32_t mask;
};

int parseDays(const char* text, DayLine* out, int max) {
  int n = 0;
  const char* p = text;
  while (*p && n < max) {
    char* end = nullptr;
    const unsigned long ym = strtoul(p, &end, 10);
    if (end == p) break;
    p = end;
    const unsigned long mask = strtoul(p, &end, 16);
    if (end == p) break;
    p = end;
    out[n++] = DayLine{static_cast<uint32_t>(ym), static_cast<uint32_t>(mask)};
    while (*p == '\n' || *p == '\r' || *p == ' ') ++p;
  }
  return n;
}

void trimPartialUtf8(std::string& s);

// Copies the first line of plain text from a (possibly HTML) definition,
// skipping tags, capped at `cap` bytes on a UTF-8 boundary.
void firstPlainLine(const std::string& def, std::string& out, size_t cap) {
  out.clear();
  bool inTag = false;
  bool started = false;
  // HTML entries open with a part-of-speech heading and the headword; the
  // meaning starts at the first list item.
  const size_t firstItem = def.find("<li>");
  for (size_t i = firstItem == std::string::npos ? 0 : firstItem; i < def.size(); ++i) {
    const char c = def[i];
    if (c == '<') {
      inTag = true;
      // Block ends after some text finish the first line.
      if (started && (def.compare(i, 4, "</li") == 0 || def.compare(i, 4, "<br") == 0 ||
                      def.compare(i, 3, "</p") == 0 || def.compare(i, 4, "</h") == 0)) {
        break;
      }
      continue;
    }
    if (inTag) {
      if (c == '>') inTag = false;
      continue;
    }
    if (c == '\n' || c == '\r') {
      if (started) break;
      continue;
    }
    if (!started && (c == ' ' || c == '\t')) continue;
    started = true;
    out.push_back(c);
    if (out.size() >= cap) break;
  }
  trimPartialUtf8(out);
}

// Drops a trailing UTF-8 sequence only when the byte cap cut it short.
void trimPartialUtf8(std::string& s) {
  size_t start = s.size();
  while (start > 0 && (static_cast<uint8_t>(s[start - 1]) & 0xC0) == 0x80) --start;
  if (start == 0) return;
  const uint8_t lead = static_cast<uint8_t>(s[start - 1]);
  size_t need = 1;
  if ((lead & 0xE0) == 0xC0) need = 2;
  else if ((lead & 0xF0) == 0xE0) need = 3;
  else if ((lead & 0xF8) == 0xF0) need = 4;
  if (s.size() - (start - 1) < need) s.erase(start - 1);
}

// First "IPA: /.../" pronunciation in a Wiktionary-style definition.
std::string extractIpa(const std::string& def) {
  const size_t at = def.find("IPA: ");
  if (at == std::string::npos) return "";
  const size_t from = at + 5;
  size_t end = def.find_first_of("<,\n", from);
  if (end == std::string::npos) end = def.size();
  std::string ipa = def.substr(from, std::min(end - from, IPA_MAX_BYTES));
  trimPartialUtf8(ipa);
  return ipa;
}

}  // namespace

void markReadToday() {
  struct tm now;
  if (!halClock.localTime(now)) return;
  const uint32_t ym = static_cast<uint32_t>((now.tm_year + 1900) * 100 + now.tm_mon + 1);
  const uint32_t bit = 1u << (now.tm_mday - 1);

  char buf[DAYS_BUF] = {};
  Storage.readFileToBuffer(DAYS_PATH, buf, sizeof(buf));
  DayLine lines[MAX_DAY_LINES + 1];
  int n = parseDays(buf, lines, MAX_DAY_LINES);
  bool found = false;
  for (int i = 0; i < n; ++i) {
    if (lines[i].yyyymm != ym) continue;
    if (lines[i].mask & bit) return;  // already marked: no SD write
    lines[i].mask |= bit;
    found = true;
    break;
  }
  if (!found) {
    if (n == MAX_DAY_LINES) {  // drop the oldest month
      memmove(lines, lines + 1, sizeof(DayLine) * (n - 1));
      --n;
    }
    lines[n++] = DayLine{ym, bit};
  }
  String out;
  for (int i = 0; i < n; ++i) {
    char line[24];
    snprintf(line, sizeof(line), "%lu %lx\n", static_cast<unsigned long>(lines[i].yyyymm),
             static_cast<unsigned long>(lines[i].mask));
    out += line;
  }
  if (!Storage.writeFile(DAYS_PATH, out)) LOG_ERR("DESK", "Failed to save reading days");
}

uint32_t readDaysMask(const int year, const int month) {
  char buf[DAYS_BUF] = {};
  if (Storage.readFileToBuffer(DAYS_PATH, buf, sizeof(buf)) == 0) return 0;
  DayLine lines[MAX_DAY_LINES];
  const int n = parseDays(buf, lines, MAX_DAY_LINES);
  const uint32_t ym = static_cast<uint32_t>(year * 100 + month);
  for (int i = 0; i < n; ++i) {
    if (lines[i].yyyymm == ym) return lines[i].mask;
  }
  return 0;
}

namespace {

// Reads the history file (at most MAX_LOOKUPS lines, ~400 bytes) into a
// transient heap string; avoids a large stack buffer on the render task.
bool readLookupFile(std::string& text) { return Storage.readFileToString("DESK", LOOKUPS_PATH, LOOKUPS_CAP, text); }

// Copies line `index` of `text` into buf. False when there is no such line.
bool lineAt(const std::string& text, int index, char* buf, size_t size) {
  size_t pos = 0;
  for (int i = 0; i < index; ++i) {
    pos = text.find('\n', pos);
    if (pos == std::string::npos) return false;
    ++pos;
  }
  if (pos >= text.size()) return false;
  size_t end = text.find('\n', pos);
  if (end == std::string::npos) end = text.size();
  size_t len = end - pos;
  if (len == 0) return false;
  if (len >= size) len = size - 1;
  memcpy(buf, text.data() + pos, len);
  buf[len] = '\0';
  return true;
}

int lineCount(const std::string& text) {
  int n = 0;
  char tmp[MAX_WORD_BYTES];
  while (n < MAX_LOOKUPS && lineAt(text, n, tmp, sizeof(tmp))) ++n;
  return n;
}

}  // namespace

void loadLookups(Lookups& out) {
  out.count = 0;
  std::string text;
  if (!readLookupFile(text)) return;
  while (out.count < SHOWN_LOOKUPS && lineAt(text, out.count, out.words[out.count], MAX_WORD_BYTES)) ++out.count;
}

void addLookup(const char* word) {
  if (!word || !*word) return;
  char clean[MAX_WORD_BYTES];
  size_t n = 0;
  for (const char* p = word; *p && n < sizeof(clean) - 1; ++p) {
    if (*p == '\n' || *p == '\r') continue;
    clean[n++] = *p;
  }
  clean[n] = '\0';
  while (n > 0 && (static_cast<uint8_t>(clean[n - 1]) & 0xC0) == 0x80) clean[--n] = '\0';
  if (n > 0 && (static_cast<uint8_t>(clean[n - 1]) & 0x80)) clean[--n] = '\0';
  if (n == 0) return;

  std::string old;
  readLookupFile(old);
  char line[MAX_WORD_BYTES];
  if (lineAt(old, 0, line, sizeof(line)) && strcmp(line, clean) == 0) return;  // unchanged: no SD write
  String out = clean;
  out += '\n';
  int written = 1;
  for (int i = 0; written < MAX_LOOKUPS && lineAt(old, i, line, sizeof(line)); ++i) {
    if (strcmp(line, clean) == 0) continue;
    out += line;
    out += '\n';
    ++written;
  }
  if (!Storage.writeFile(LOOKUPS_PATH, out)) LOG_ERR("DESK", "Failed to save lookups");
}

namespace {
// Pronunciation from the first other indexed dictionary that has one
// (e.g. Wiktionary English), since EJDict carries none.
std::string ipaFromOtherDictionaries(const std::string& word, const char* skipName) {
  std::vector<DictionaryEntry> dicts;
  DictionaryRegistry::discover(dicts);
  for (const auto& entry : dicts) {
    if (entry.name == skipName) continue;
    auto dict = std::unique_ptr<Dictionary>(new (std::nothrow) Dictionary());
    if (!dict || !dict->open(entry.name.c_str()) || dict->needsIndex()) continue;
    std::string definition;
    std::string headword;
    if (!dict->lookup(word.c_str(), definition, headword)) continue;
    std::string ipa = extractIpa(definition);
    if (!ipa.empty()) return ipa;
  }
  return "";
}
}  // namespace

bool wordOfDay(const uint32_t yyyymmdd, const char* dictName, std::string& word, std::string& ipa,
               std::string& meaning) {
  word.clear();
  ipa.clear();
  meaning.clear();
  if (!dictName) dictName = "";

  // Cache line: "<yyyymmdd>\t<dict>\t<word>\t<ipa>\t<meaning>"
  std::string cache;
  if (Storage.readFileToString("DESK", WOTD_PATH, 512, cache)) {
    char prefix[24];
    snprintf(prefix, sizeof(prefix), "%lu\t", static_cast<unsigned long>(yyyymmdd));
    const size_t dictEnd = cache.find('\t', strlen(prefix));
    if (cache.rfind(prefix, 0) == 0 && dictEnd != std::string::npos &&
        cache.compare(strlen(prefix), dictEnd - strlen(prefix), dictName) == 0) {
      const size_t wordEnd = cache.find('\t', dictEnd + 1);
      const size_t ipaEnd = wordEnd == std::string::npos ? wordEnd : cache.find('\t', wordEnd + 1);
      if (ipaEnd != std::string::npos) {
        word = cache.substr(dictEnd + 1, wordEnd - dictEnd - 1);
        ipa = cache.substr(wordEnd + 1, ipaEnd - wordEnd - 1);
        meaning = cache.substr(ipaEnd + 1);
        while (!meaning.empty() && (meaning.back() == '\n' || meaning.back() == '\r')) meaning.pop_back();
        return !word.empty();
      }
    }
  }

  // Not cached for today: pick one now. Prefer reviewing an older lookup.
  {
    std::string history;
    readLookupFile(history);
    const int count = lineCount(history);
    if (count > 0) {
      const int first = count > SHOWN_LOOKUPS ? SHOWN_LOOKUPS : 0;
      char pick[MAX_WORD_BYTES];
      if (lineAt(history, first + static_cast<int>(yyyymmdd % static_cast<uint32_t>(count - first)), pick,
                 sizeof(pick))) {
        word = pick;
      }
    }
  }
  // Heap-held: Dictionary carries a 256-byte word buffer, and the definition
  // (freed right after the first line is taken) can be several KB.
  auto dict = std::unique_ptr<Dictionary>(new (std::nothrow) Dictionary());
  const bool dictOk = dict && dict->open(dictName) && !dict->needsIndex();
  std::string definition;
  std::string headword;
  if (word.empty()) {
    if (!dictOk) return false;
    for (uint32_t attempt = 0; attempt < 6; ++attempt) {
      if (!dict->sampleHeadword(yyyymmdd * 2654435761u + attempt * 40503u, word)) return false;
      if (dict->lookup(word.c_str(), definition, headword)) break;
      word.clear();
    }
    if (word.empty()) return false;
  } else if (dictOk && !dict->lookup(word.c_str(), definition, headword)) {
    definition.clear();
  }
  dict.reset();
  firstPlainLine(definition, meaning, MEANING_MAX_BYTES);
  ipa = extractIpa(definition);
  std::string().swap(definition);
  if (ipa.empty()) ipa = ipaFromOtherDictionaries(word, dictName);

  String out;
  char prefix[24];
  snprintf(prefix, sizeof(prefix), "%lu\t", static_cast<unsigned long>(yyyymmdd));
  out += prefix;
  out += dictName;
  out += '\t';
  out += word.c_str();
  out += '\t';
  out += ipa.c_str();
  out += '\t';
  out += meaning.c_str();
  out += '\n';
  if (!Storage.writeFile(WOTD_PATH, out)) LOG_ERR("DESK", "Failed to cache word of the day");
  return true;
}

}  // namespace desk_stats
