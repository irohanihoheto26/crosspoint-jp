#pragma once

#include <cstdint>
#include <string>

// Small persistent records behind the Desk home screen: which days of a month
// had reading, the last few dictionary lookups, and the cached word of the day.
// Everything lives in tiny text files under /.crosspoint so a write is a few
// dozen bytes and only happens on the events themselves (book open, lookup,
// first desk render of a day) — never per page turn.
namespace desk_stats {

constexpr int MAX_LOOKUPS = 4;
constexpr size_t MAX_WORD_BYTES = 48;

struct Lookups {
  char words[MAX_LOOKUPS][MAX_WORD_BYTES] = {};
  int count = 0;
};

// Marks today's date (from the RTC) as a reading day. No-op without a clock
// or when today is already marked.
void markReadToday();

// Bit (day - 1) is set for each day of the month that had reading.
uint32_t readDaysMask(int year, int month);

// Records a looked-up headword (newest first, deduplicated, MAX_LOOKUPS kept).
void addLookup(const char* word);
void loadLookups(Lookups& out);

// Word of the day for `yyyymmdd` from the dictionary folder `dictName`.
// Picks once per day and caches the word plus a one-line meaning; later calls
// that day just read the cache. False when no dictionary is usable.
bool wordOfDay(uint32_t yyyymmdd, const char* dictName, std::string& word, std::string& meaning);

}  // namespace desk_stats
