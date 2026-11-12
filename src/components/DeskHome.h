#pragma once

#include <cstdint>
#include <vector>

#include "RecentBooksStore.h"

class GfxRenderer;

// Desk home: a Licorice-style home screen. Top row of shortcut icons, a large
// clock and date, the three most recent covers, then a calendar of reading
// days, recent dictionary lookups, the moon phase and a word of the day.
// Everything is drawn once per home render; the clock only changes when the
// screen is redrawn.
namespace desk_home {

constexpr int MAX_BOOKS = 3;

// Height of the cover tiles, used to generate matching thumbnails.
int coverHeight(const GfxRenderer& renderer);

// selectorIndex: 0..books-1 selects a cover, books.. selects icons[i - books].
void draw(GfxRenderer& renderer, const std::vector<RecentBook>& books, int selectorIndex,
          const uint8_t* const* icons, int iconCount);

}  // namespace desk_home
