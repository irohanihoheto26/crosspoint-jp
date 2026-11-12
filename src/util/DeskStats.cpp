#include "DeskStats.h"

#include <HalClock.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <new>

#include "Dictionary.h"

namespace desk_stats {
namespace {

constexpr char DAYS_PATH[] = "/.crosspoint/desk_days.txt";
constexpr char LOOKUPS_PATH[] = "/.crosspoint/desk_words.txt";
constexpr char WOTD_PATH[] = "/.crosspoint/desk_wotd.txt";
// A few years of "YYYYMM HHHHHHHH\n" lines; older months are dropped on write.
constexpr int MAX_DAY_LINES = 36;
constexpr size_t DAYS_BUF = MAX_DAY_LINES * 16 + 1;
constexpr size_t LOOKUPS_BUF = MAX_LOOKUPS * MAX_WORD_BYTES + 8;
constexpr size_t MEANING_MAX_BYTES = 90;

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

// Copies the first line of plain text from a (possibly HTML) definition,
// skipping tags, capped at `cap` bytes on a UTF-8 boundary.
void firstPlainLine(const std::string& def, std::string& out, size_t cap) {
  out.clear();
  bool inTag = false;
  bool started = false;
  for (size_t i = 0; i < def.size(); ++i) {
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
  // Trim a split multi-byte sequence.
  while (!out.empty() && (static_cast<uint8_t>(out.back()) & 0xC0) == 0x80) out.pop_back();
  if (!out.empty() && (static_cast<uint8_t>(out.back()) & 0x80)) out.pop_back();
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

void loadLookups(Lookups& out) {
  out.count = 0;
  char buf[LOOKUPS_BUF] = {};
  if (Storage.readFileToBuffer(LOOKUPS_PATH, buf, sizeof(buf)) == 0) return;
  const char* p = buf;
  while (*p && out.count < MAX_LOOKUPS) {
    const char* nl = strchr(p, '\n');
    const size_t len = nl ? static_cast<size_t>(nl - p) : strlen(p);
    if (len > 0) {
      const size_t n = len < MAX_WORD_BYTES - 1 ? len : MAX_WORD_BYTES - 1;
      memcpy(out.words[out.count], p, n);
      out.words[out.count][n] = '\0';
      ++out.count;
    }
    if (!nl) break;
    p = nl + 1;
  }
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

  Lookups old;
  loadLookups(old);
  if (old.count > 0 && strcmp(old.words[0], clean) == 0) return;  // unchanged: no SD write
  String out = clean;
  out += '\n';
  int written = 1;
  for (int i = 0; i < old.count && written < MAX_LOOKUPS; ++i) {
    if (strcmp(old.words[i], clean) == 0) continue;
    out += old.words[i];
    out += '\n';
    ++written;
  }
  if (!Storage.writeFile(LOOKUPS_PATH, out)) LOG_ERR("DESK", "Failed to save lookups");
}

bool wordOfDay(const uint32_t yyyymmdd, const char* dictName, std::string& word, std::string& meaning) {
  word.clear();
  meaning.clear();
  if (!dictName || !*dictName) return false;

  // Cache line: "<yyyymmdd>\t<dict>\t<word>\t<meaning>"
  std::string cache;
  if (Storage.readFileToString("DESK", WOTD_PATH, 512, cache)) {
    char prefix[24];
    snprintf(prefix, sizeof(prefix), "%lu\t", static_cast<unsigned long>(yyyymmdd));
    const size_t dictEnd = cache.find('\t', strlen(prefix));
    if (cache.rfind(prefix, 0) == 0 && dictEnd != std::string::npos &&
        cache.compare(strlen(prefix), dictEnd - strlen(prefix), dictName) == 0) {
      const size_t wordEnd = cache.find('\t', dictEnd + 1);
      if (wordEnd != std::string::npos) {
        word = cache.substr(dictEnd + 1, wordEnd - dictEnd - 1);
        meaning = cache.substr(wordEnd + 1);
        while (!meaning.empty() && (meaning.back() == '\n' || meaning.back() == '\r')) meaning.pop_back();
        return !word.empty();
      }
    }
  }

  // Not cached for today: pick one now. Heap-held: Dictionary carries a 256-byte
  // word buffer and the lookup output can be several KB.
  auto dict = std::unique_ptr<Dictionary>(new (std::nothrow) Dictionary());
  if (!dict || !dict->open(dictName) || dict->needsIndex()) return false;
  std::string definition;
  std::string headword;
  for (uint32_t attempt = 0; attempt < 6; ++attempt) {
    if (!dict->sampleHeadword(yyyymmdd * 2654435761u + attempt * 40503u, word)) return false;
    if (dict->lookup(word.c_str(), definition, headword)) break;
    word.clear();
  }
  if (word.empty()) return false;
  firstPlainLine(definition, meaning, MEANING_MAX_BYTES);
  std::string().swap(definition);

  String out;
  char prefix[24];
  snprintf(prefix, sizeof(prefix), "%lu\t", static_cast<unsigned long>(yyyymmdd));
  out += prefix;
  out += dictName;
  out += '\t';
  out += word.c_str();
  out += '\t';
  out += meaning.c_str();
  out += '\n';
  if (!Storage.writeFile(WOTD_PATH, out)) LOG_ERR("DESK", "Failed to cache word of the day");
  return true;
}

}  // namespace desk_stats
