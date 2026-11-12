#pragma once

#include <cstdint>
#include <string>

// Small persistent records behind the Desk home screen: which days of a month
// had reading, the last few dictionary lookups, and the cached word of the day.
// Everything lives in tiny text files under /.crosspoint so a write is a few
// dozen bytes and only happens on the events themselves (book open, lookup,
// first desk render of a day) — never per page turn.
namespace desk_stats {

// History kept on SD; the home box shows the newest SHOWN_LOOKUPS and the
// word of the day reviews one of the older ones.
constexpr int MAX_LOOKUPS = 10;
constexpr int SHOWN_LOOKUPS = 4;
constexpr size_t MAX_WORD_BYTES = 40;

struct Lookups {
  char words[SHOWN_LOOKUPS][MAX_WORD_BYTES] = {};
  int count = 0;
};

// Marks today's date (from the RTC) as a reading day. No-op without a clock
// or when today is already marked.
void markReadToday();

// Bit (day - 1) is set for each day of the month that had reading.
uint32_t readDaysMask(int year, int month);

// Records a looked-up headword (newest first, deduplicated, MAX_LOOKUPS kept).
void addLookup(const char* word);
// Newest SHOWN_LOOKUPS entries.
void loadLookups(Lookups& out);

// Word of the day for `yyyymmdd`: one of the older lookups (the ones not shown
// in the recent box), else a random headword of `dictName`. Picks once per day
// and caches the word plus a one-line meaning; later calls that day just read
// the cache. False when there is nothing to show.
bool wordOfDay(uint32_t yyyymmdd, const char* dictName, std::string& word, std::string& meaning);

}  // namespace desk_stats
