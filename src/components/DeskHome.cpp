#include "DeskHome.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <I18n.h>
#include <HalPowerManager.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

#include "CrossPointSettings.h"
#include "components/UITheme.h"
#include "components/themes/BaseTheme.h"
#include "fontIds.h"
#include "util/BookProgress.h"
#include "util/DeskStats.h"

namespace desk_home {
namespace {

constexpr int SIDE = 16;
constexpr int TOP = 10;
constexpr int ICON = 32;
constexpr int ICON_BOX = 44;
constexpr int ICON_GAP = 8;
constexpr int CLOCK_GAP_ABOVE = 30;
constexpr int CLOCK_GAP_BELOW = 26;
constexpr int COVER_GAP = 12;
constexpr int WIDGET_GAP = 8;
constexpr int BOTTOM = 10;
constexpr int BOX_PAD = 6;
constexpr int BOX_RADIUS = 6;
constexpr double SYNODIC_MONTH = 29.530588853;

struct Layout {
  int width = 0;
  int coversTop = 0;
  int coverW = 0;
  int coverH = 0;
  int coversX = 0;
  int widgetsTop = 0;
  int widgetsH = 0;
  int clockTop = 0;
  int dateTop = 0;
};

int textLine(const GfxRenderer& r) { return r.getLineHeight(DESK_TEXT_FONT_ID); }

// Calendar: title + weekday row + 6 week rows.
int calendarHeight(const GfxRenderer& r) { return BOX_PAD * 2 + textLine(r) + 7 * (textLine(r) + 3); }
// Recent lookups: title + two rows of words.
int wordsHeight(const GfxRenderer& r) { return BOX_PAD * 2 + textLine(r) * 3 + 4; }

Layout computeLayout(const GfxRenderer& r) {
  Layout l;
  l.width = r.getScreenWidth();
  const int height = r.getScreenHeight();
  l.clockTop = TOP + ICON_BOX + CLOCK_GAP_ABOVE;
  l.dateTop = l.clockTop + r.getLineHeight(DESK_CLOCK_FONT_ID) + 4;
  l.coversTop = l.dateTop + r.getLineHeight(DESK_DATE_FONT_ID) + CLOCK_GAP_BELOW;
  l.widgetsH = calendarHeight(r) + WIDGET_GAP + wordsHeight(r);
  l.widgetsTop = height - BOTTOM - l.widgetsH;
  const int tileW = (l.width - 2 * SIDE - 2 * COVER_GAP) / 3;
  // 10px under the covers for the selection frame.
  const int maxH = l.widgetsTop - COVER_GAP - 10 - l.coversTop;
  l.coverH = std::max(40, std::min(tileW * 3 / 2, maxH));
  l.coverW = std::min(tileW, l.coverH * 2 / 3);
  l.coversX = (l.width - (3 * l.coverW + 2 * COVER_GAP)) / 2;
  return l;
}

void centeredText(const GfxRenderer& r, int font, int x, int w, int y, const char* text, bool black = true) {
  const int tw = r.getTextWidth(font, text);
  r.drawText(font, x + (w - tw) / 2, y, text, black);
}

void box(const GfxRenderer& r, int x, int y, int w, int h) { r.drawRoundedRect(x, y, w, h, 1, BOX_RADIUS, true); }

// Splits "a|b|c" and returns part `index` into buf.
void pipePart(const char* list, int index, char* buf, size_t size) {
  buf[0] = '\0';
  const char* p = list;
  for (int i = 0; i < index && p; ++i) {
    p = strchr(p, '|');
    if (p) ++p;
  }
  if (!p) return;
  const char* end = strchr(p, '|');
  size_t len = end ? static_cast<size_t>(end - p) : strlen(p);
  if (len >= size) len = size - 1;
  memcpy(buf, p, len);
  buf[len] = '\0';
}

void drawTopRow(const GfxRenderer& r, const Layout& l, int selectedIcon, const uint8_t* const* icons, int iconCount) {
  for (int i = 0; i < iconCount; ++i) {
    const int x = SIDE + i * (ICON_BOX + ICON_GAP);
    r.drawIcon(icons[i], x + (ICON_BOX - ICON) / 2, TOP + (ICON_BOX - ICON) / 2, ICON);
    if (i == selectedIcon) {
      r.drawRoundedRect(x, TOP, ICON_BOX, ICON_BOX, 3, BOX_RADIUS, true);
    } else {
      r.drawRoundedRect(x, TOP, ICON_BOX, ICON_BOX, 1, BOX_RADIUS, true);
    }
  }
  char battery[8];
  snprintf(battery, sizeof(battery), "%u%%", static_cast<unsigned>(powerManager.getBatteryPercentage()));
  const int bw = r.getTextWidth(DESK_TEXT_FONT_ID, battery);
  r.drawText(DESK_TEXT_FONT_ID, l.width - SIDE - bw, TOP + (ICON_BOX - textLine(r)) / 2, battery);
}

void drawClock(const GfxRenderer& r, const Layout& l, const struct tm* now) {
  char clock[8];
  if (now) {
    const bool twelve = SETTINGS.clockFormat == 1;
    int hour = now->tm_hour;
    if (twelve) hour = hour % 12 == 0 ? 12 : hour % 12;
    snprintf(clock, sizeof(clock), "%d:%02d", hour, now->tm_min);
  } else {
    snprintf(clock, sizeof(clock), "%s", tr(STR_DESK_NO_CLOCK));
  }
  centeredText(r, DESK_CLOCK_FONT_ID, 0, l.width, l.clockTop, clock);
  if (!now) return;
  char weekday[16];
  pipePart(tr(STR_DESK_WEEKDAYS), now->tm_wday, weekday, sizeof(weekday));
  char date[48];
  snprintf(date, sizeof(date), tr(STR_DESK_DATE_FMT), now->tm_mon + 1, now->tm_mday, weekday);
  centeredText(r, DESK_DATE_FONT_ID, 0, l.width, l.dateTop, date);
}

void drawCovers(GfxRenderer& r, const Layout& l, const std::vector<RecentBook>& books, int selected) {
  const int count = std::min(static_cast<int>(books.size()), MAX_BOOKS);
  for (int i = 0; i < count; ++i) {
    const int x = l.coversX + i * (l.coverW + COVER_GAP);
    const int y = l.coversTop;
    bool drawn = false;
    if (!books[i].coverBmpPath.empty()) {
      HalFile file;
      if (Storage.openFileForRead("DESK", UITheme::getCoverThumbPath(books[i].coverBmpPath, l.coverH), file)) {
        Bitmap bitmap(file);
        if (bitmap.parseHeaders() == BmpReaderError::Ok) {
          drawn = BaseTheme::drawCoverThumbFill(r, bitmap, Rect{x, y, l.coverW, l.coverH});
        }
        file.close();
      }
    }
    if (!drawn) {
      // No cover: title on a plain card.
      const auto lines = r.wrappedText(UI_10_FONT_ID, books[i].title.c_str(), l.coverW - 12, 5);
      int ty = y + 8;
      for (const auto& line : lines) {
        r.drawText(UI_10_FONT_ID, x + 6, ty, line.c_str());
        ty += r.getLineHeight(UI_10_FONT_ID);
      }
    }
    r.drawRect(x, y, l.coverW, l.coverH, true);

    const int progress = loadBookProgress(books[i].path);
    if (progress >= 0) {
      char pct[8];
      snprintf(pct, sizeof(pct), "%d%%", progress);
      const int pw = r.getTextWidth(DESK_TEXT_FONT_ID, pct) + 6;
      const int ph = textLine(r) + 2;
      r.fillRect(x + l.coverW - pw, y, pw, ph, true);
      r.drawText(DESK_TEXT_FONT_ID, x + l.coverW - pw + 3, y + 1, pct, false);
    }
    if (i == selected) r.drawRect(x - 5, y - 5, l.coverW + 10, l.coverH + 10, 3, true);
  }
  if (count == 0) {
    centeredText(r, UI_12_FONT_ID, 0, l.width, l.coversTop + l.coverH / 2, tr(STR_NO_OPEN_BOOK));
  }
}

void drawCalendar(const GfxRenderer& r, int x, int y, int w, int h, const struct tm* now) {
  box(r, x, y, w, h);
  if (!now) return;
  const int line = textLine(r);
  const int year = now->tm_year + 1900;
  const int month = now->tm_mon + 1;
  char title[24];
  snprintf(title, sizeof(title), tr(STR_DESK_CAL_TITLE), year, month);
  r.drawText(DESK_TEXT_FONT_ID, x + BOX_PAD, y + BOX_PAD, title);

  struct tm first = *now;
  first.tm_mday = 1;
  first.tm_hour = 12;
  mktime(&first);
  const int startWday = first.tm_wday;
  static constexpr int DAYS[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int days = DAYS[now->tm_mon];
  if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) days = 29;
  const uint32_t readMask = desk_stats::readDaysMask(year, month);

  const int cellW = (w - 2 * BOX_PAD) / 7;
  const int cellH = line + 3;
  const int gridX = x + BOX_PAD + (w - 2 * BOX_PAD - 7 * cellW) / 2;
  int rowY = y + BOX_PAD + line;
  char label[8];
  for (int d = 0; d < 7; ++d) {
    pipePart(tr(STR_DESK_WEEK_INITIALS), d, label, sizeof(label));
    centeredText(r, DESK_TEXT_FONT_ID, gridX + d * cellW, cellW, rowY, label);
  }
  rowY += cellH;
  for (int day = 1; day <= days; ++day) {
    const int slot = startWday + day - 1;
    const int cx = gridX + (slot % 7) * cellW;
    const int cy = rowY + (slot / 7) * cellH;
    snprintf(label, sizeof(label), "%d", day);
    const bool read = (readMask >> (day - 1)) & 1u;
    if (read) r.fillRect(cx + 1, cy - 1, cellW - 2, cellH - 1, true);
    centeredText(r, DESK_TEXT_FONT_ID, cx, cellW, cy, label, !read);
    if (day == now->tm_mday) r.drawRect(cx, cy - 2, cellW, cellH + 1, 2, true);
  }
}

void drawLookups(const GfxRenderer& r, int x, int y, int w, int h) {
  box(r, x, y, w, h);
  const int line = textLine(r);
  r.drawText(DESK_TEXT_FONT_ID, x + BOX_PAD, y + BOX_PAD, tr(STR_DESK_RECENT_WORDS));
  desk_stats::Lookups lookups;
  desk_stats::loadLookups(lookups);
  if (lookups.count == 0) {
    r.drawText(DESK_TEXT_FONT_ID, x + BOX_PAD, y + BOX_PAD + line + 2, tr(STR_DESK_NO_WORDS));
    return;
  }
  const int colW = (w - 2 * BOX_PAD) / 2;
  for (int i = 0; i < lookups.count; ++i) {
    const int cx = x + BOX_PAD + (i % 2) * colW;
    const int cy = y + BOX_PAD + line + 2 + (i / 2) * line;
    const std::string text = r.truncatedText(UI_10_FONT_ID, lookups.words[i], colW - 4);
    r.drawText(UI_10_FONT_ID, cx, cy, text.c_str());
  }
}

void drawMoon(GfxRenderer& r, int x, int y, int w, int h) {
  box(r, x, y, w, h);
  const time_t now = time(nullptr);
  if (now < 1600000000) return;  // clock not set
  const double jd = static_cast<double>(now) / 86400.0 + 2440587.5;
  double age = std::fmod(jd - 2451550.1, SYNODIC_MONTH);
  if (age < 0) age += SYNODIC_MONTH;
  static constexpr StrId PHASES[] = {StrId::STR_MOON_NEW,           StrId::STR_MOON_WAXING_CRESCENT,
                                     StrId::STR_MOON_FIRST_QUARTER, StrId::STR_MOON_WAXING_GIBBOUS,
                                     StrId::STR_MOON_FULL,          StrId::STR_MOON_WANING_GIBBOUS,
                                     StrId::STR_MOON_LAST_QUARTER,  StrId::STR_MOON_WANING_CRESCENT};
  const int phase = static_cast<int>(age / SYNODIC_MONTH * 8.0 + 0.5) % 8;
  const int line = textLine(r);
  const int ageTenths = static_cast<int>(age * 10.0 + 0.5);
  char text[40];
  snprintf(text, sizeof(text), tr(STR_DESK_MOON_AGE), ageTenths / 10, ageTenths % 10);
  centeredText(r, DESK_TEXT_FONT_ID, x, w, y + BOX_PAD, text);
  centeredText(r, DESK_TEXT_FONT_ID, x, w, y + h - BOX_PAD - line, I18N.get(PHASES[phase]));

  const int radius = std::max(6, std::min(w / 2 - BOX_PAD, (h - 2 * BOX_PAD - 2 * line) / 2 - 3));
  const int cx = x + w / 2;
  const int cy = y + BOX_PAD + line + (h - 2 * BOX_PAD - 2 * line) / 2;
  const double angle = age / SYNODIC_MONTH * 2.0 * M_PI;
  const double terminator = std::cos(angle);
  const bool waxing = angle < M_PI;
  for (int dy = -radius; dy <= radius; ++dy) {
    const int half = static_cast<int>(std::sqrt(static_cast<double>(radius * radius - dy * dy)) + 0.5);
    if (half <= 0) continue;
    // Dark part of the disc (northern-hemisphere view).
    int from, to;
    if (waxing) {
      from = -half;
      to = static_cast<int>(half * terminator);
    } else {
      from = static_cast<int>(-half * terminator);
      to = half;
    }
    if (to > from) r.fillRect(cx + from, cy + dy, to - from, 1, true);
    r.drawPixel(cx - half, cy + dy, true);
    r.drawPixel(cx + half, cy + dy, true);
  }
  r.drawPixel(cx, cy - radius, true);
  r.drawPixel(cx, cy + radius, true);
}

void drawWordOfDay(const GfxRenderer& r, int x, int y, int w, int h, const struct tm* now) {
  box(r, x, y, w, h);
  const int line = textLine(r);
  centeredText(r, DESK_TEXT_FONT_ID, x, w, y + BOX_PAD, tr(STR_DESK_WORD_OF_DAY));
  std::string word;
  std::string meaning;
  const uint32_t ymd =
      now ? static_cast<uint32_t>((now->tm_year + 1900) * 10000 + (now->tm_mon + 1) * 100 + now->tm_mday) : 0;
  if (!now || !desk_stats::wordOfDay(ymd, SETTINGS.dictionaryName, word, meaning)) {
    centeredText(r, DESK_TEXT_FONT_ID, x, w, y + h / 2 - line / 2, tr(STR_DESK_NO_DICTIONARY));
    return;
  }
  const int wordY = y + BOX_PAD + line + 4;
  const std::string shownWord = r.truncatedText(UI_12_FONT_ID, word.c_str(), w - 2 * BOX_PAD);
  centeredText(r, UI_12_FONT_ID, x, w, wordY, shownWord.c_str());
  if (meaning.empty()) return;
  const auto lines = r.wrappedText(UI_10_FONT_ID, meaning.c_str(), w - 2 * BOX_PAD, 2);
  int my = wordY + r.getLineHeight(UI_12_FONT_ID) + 2;
  for (const auto& text : lines) {
    if (my + r.getLineHeight(UI_10_FONT_ID) > y + h - 2) break;
    centeredText(r, UI_10_FONT_ID, x, w, my, text.c_str());
    my += r.getLineHeight(UI_10_FONT_ID);
  }
}

}  // namespace

int coverHeight(const GfxRenderer& renderer) { return computeLayout(renderer).coverH; }

void draw(GfxRenderer& renderer, const std::vector<RecentBook>& books, const int selectorIndex,
          const uint8_t* const* icons, const int iconCount) {
  const Layout l = computeLayout(renderer);
  struct tm nowTm;
  const struct tm* now = halClock.isAvailable() && halClock.localTime(nowTm) ? &nowTm : nullptr;
  const int bookCount = std::min(static_cast<int>(books.size()), MAX_BOOKS);

  drawTopRow(renderer, l, selectorIndex - bookCount, icons, iconCount);
  drawClock(renderer, l, now);
  drawCovers(renderer, l, books, selectorIndex);

  const int innerW = l.width - 2 * SIDE;
  const int leftW = (innerW - WIDGET_GAP) * 58 / 100;
  const int rightW = innerW - WIDGET_GAP - leftW;
  const int calH = calendarHeight(renderer);
  const int wordsH = wordsHeight(renderer);
  const int rightX = SIDE + leftW + WIDGET_GAP;
  const int rightTopH = (l.widgetsH - WIDGET_GAP) / 2;
  drawCalendar(renderer, SIDE, l.widgetsTop, leftW, calH, now);
  drawLookups(renderer, SIDE, l.widgetsTop + calH + WIDGET_GAP, leftW, wordsH);
  drawMoon(renderer, rightX, l.widgetsTop, rightW, rightTopH);
  drawWordOfDay(renderer, rightX, l.widgetsTop + rightTopH + WIDGET_GAP, rightW,
                l.widgetsH - rightTopH - WIDGET_GAP, now);
}

}  // namespace desk_home
