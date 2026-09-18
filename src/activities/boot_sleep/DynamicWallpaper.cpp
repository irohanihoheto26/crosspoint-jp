#include "DynamicWallpaper.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cstdio>

#include "CrossPointSettings.h"
#include "fontIds.h"

namespace {

// レイアウトは X4 の 480×800 を基準に書き、実際の画面寸法へ比例変換する（X3 は 528×792）。
constexpr int BASE_W = 480;
constexpr int BASE_H = 800;

// 波形用: 1 周期 64 分割、振幅 1024
constexpr int16_t SIN64[64] = {0,    100,  200,  297,  392,  483,  569,  650,   724,   792,   851,   903,   946,
                               980,  1004, 1019, 1024, 1019, 1004, 980,  946,   903,   851,   792,   724,   650,
                               569,  483,  392,  297,  200,  100,  0,    -100,  -200,  -297,  -392,  -483,  -569,
                               -650, -724, -792, -851, -903, -946, -980, -1004, -1019, -1024, -1019, -1004, -980,
                               -946, -903, -851, -792, -724, -650, -569, -483,  -392,  -297,  -200,  -100};

constexpr uint32_t fnv1a(uint32_t v) {
  uint32_t h = 2166136261u;
  for (int i = 0; i < 4; i++) {
    h ^= (v >> (i * 8)) & 0xFF;
    h *= 16777619u;
  }
  return h;
}

int isqrt(int v) {
  int r = 0;
  while ((r + 1) * (r + 1) <= v) r++;
  return r;
}

constexpr bool isLeapYear(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

constexpr int daysInMonth(int y, int m) {
  constexpr int DAYS[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return (m == 2 && isLeapYear(y)) ? 29 : DAYS[m - 1];
}

}  // namespace

DynamicWallpaper::YearInfo DynamicWallpaper::computeYearInfo(const struct tm& date) {
  YearInfo info{};
  info.year = date.tm_year + 1900;
  info.month = date.tm_mon + 1;
  info.day = date.tm_mday;
  info.daysInYear = isLeapYear(info.year) ? 366 : 365;
  // tm_yday は 0 始まりだが、localtime_r 以外の経路で未設定の可能性もあるので月日から求める
  int doy = info.day;
  for (int m = 1; m < info.month; m++) doy += daysInMonth(info.year, m);
  info.dayOfYear = std::clamp(doy, 1, info.daysInYear);
  info.done = info.dayOfYear - 1;
  return info;
}

void DynamicWallpaper::render(const GfxRenderer& renderer, const uint8_t style, const struct tm& date) {
  const YearInfo info = computeYearInfo(date);
  switch (style) {
    case CrossPointSettings::DW_WATER_LEVEL:
      renderWaterLevel(renderer, info);
      break;
    case CrossPointSettings::DW_SQUARE_GRID:
      renderSquareGrid(renderer, info);
      break;
    case CrossPointSettings::DW_DOT_GRID:
    default:
      renderDotGrid(renderer, info);
      break;
  }
}

void DynamicWallpaper::fillCircle(const GfxRenderer& renderer, const int cx, const int cy, const int r,
                                  const Color color) {
  for (int dy = -r; dy <= r; dy++) {
    const int hw = isqrt(r * r - dy * dy);
    renderer.fillRectDither(cx - hw, cy + dy, 2 * hw + 1, 1, color);
  }
}

// ラベルを 1 文字ずつ字間を空けて描く（小さな大文字の見出し用）。幅を返す。
int DynamicWallpaper::drawTracked(const GfxRenderer& renderer, const int fontId, const int x, const int top,
                                  const char* text, const int tracking, const bool draw, const bool black) {
  int cx = x;
  char ch[2] = {0, 0};
  for (const char* p = text; *p; p++) {
    if (*p == ' ') {
      // 空白は字形が無く getTextWidth が 0 になるので、送り幅で空ける
      cx += renderer.getSpaceWidth(fontId) + tracking;
      continue;
    }
    ch[0] = *p;
    if (draw) renderer.drawText(fontId, cx, top, ch, black);
    cx += renderer.getTextWidth(fontId, ch) + tracking;
  }
  return cx - x - tracking;
}

// 見出し: 左は大きな残り日数の右脇に「DAYS / LEFT」を 2 行で寄せた組み、
// 右は「70.6%」を同じベースラインに右揃え。その下に細い罫線。
// ラベルはデザインの一部として英字の小見出しに固定している（数字フォントも欧文のみ）。
// yTop は大きな数字の上端。left/right は見出しの左右端。
void DynamicWallpaper::drawHeader(const GfxRenderer& renderer, const YearInfo& info, const int left, const int right,
                                  const int yTop, const bool black) {
  const int W = renderer.getScreenWidth();
  const auto sx = [W](int v) { return v * W / BASE_W; };

  char daysBuf[8];
  snprintf(daysBuf, sizeof(daysBuf), "%d", info.daysInYear - info.dayOfYear);
  // 経過率は 0.1% 単位（過ぎた日数 ÷ 年の日数）
  const int pct10 = info.done * 1000 / info.daysInYear;
  char pctIntBuf[8];
  snprintf(pctIntBuf, sizeof(pctIntBuf), "%d", pct10 / 10);
  char pctFracBuf[8];
  snprintf(pctFracBuf, sizeof(pctFracBuf), ".%d%%", pct10 % 10);

  // 26pt Noto Sans Bold の数字は約 41px。罫線はベースラインの 12px 下。
  const int baseline = yTop + sx(41);
  const int tracking = std::max(1, sx(1));
  const int labelTop = baseline - renderer.getFontAscenderSize(WALLPAPER_8_FONT_ID);
  const int labelPitch = sx(15);  // ラベル 2 行の行送り

  // 左: 大きな数字 ＋ 右脇に「DAYS / LEFT」（下の行を数字のベースラインに揃える）
  // 数字はサイドベアリングぶん左にずらし、墨の左端を罫線の左端に揃える
  const int daysBearing = renderer.getTextLeftBearing(WALLPAPER_26_FONT_ID, daysBuf);
  const int daysW = renderer.getTextWidth(WALLPAPER_26_FONT_ID, daysBuf);
  renderer.drawText(WALLPAPER_26_FONT_ID, left - daysBearing,
                    baseline - renderer.getFontAscenderSize(WALLPAPER_26_FONT_ID), daysBuf, black);
  const int labelX = left + daysW + sx(10);
  drawTracked(renderer, WALLPAPER_8_FONT_ID, labelX, labelTop - labelPitch, "DAYS", tracking, true, black);
  drawTracked(renderer, WALLPAPER_8_FONT_ID, labelX, labelTop, "LEFT", tracking, true, black);

  // 右: 「70.6%」を同じベースラインに、墨の右端を罫線の右端に揃える。
  // 整数部は 13pt、小数点以下と % は 8pt に落として整数部を主役にする。
  const int fracBearing = renderer.getTextLeftBearing(WALLPAPER_8_FONT_ID, pctFracBuf);
  const int fracW = renderer.getTextWidth(WALLPAPER_8_FONT_ID, pctFracBuf);
  const int fracX = right - fracBearing - fracW;
  renderer.drawText(WALLPAPER_8_FONT_ID, fracX, baseline - renderer.getFontAscenderSize(WALLPAPER_8_FONT_ID),
                    pctFracBuf, black);
  const int intBearing = renderer.getTextLeftBearing(WALLPAPER_13_FONT_ID, pctIntBuf);
  const int intW = renderer.getTextWidth(WALLPAPER_13_FONT_ID, pctIntBuf);
  renderer.drawText(WALLPAPER_13_FONT_ID, fracX - sx(1) - intBearing - intW,
                    baseline - renderer.getFontAscenderSize(WALLPAPER_13_FONT_ID), pctIntBuf, black);

  // 罫線
  renderer.fillRect(left, baseline + sx(12), right - left, 1, black);
}

// 01 年の水位: 画面下から黒が満ちてくる。黒の高さ＝経過割合。
// 右端の目盛りは月の境、水面直下の濃いディザ帯は今月の経過分。
void DynamicWallpaper::renderWaterLevel(const GfxRenderer& renderer, const YearInfo& info) {
  const int W = renderer.getScreenWidth();
  const int H = renderer.getScreenHeight();
  const auto sx = [W](int v) { return v * W / BASE_W; };
  const auto sy = [H](int v) { return v * H / BASE_H; };
  const int Y = info.daysInYear;

  renderer.clearScreen();

  const int water = H - info.done * H / Y;
  const int monthTop = std::min(H, water + (info.day - 1) * H / Y);

  // 波の形だけ日付で変える
  const uint32_t hash = fnv1a(static_cast<uint32_t>(info.year * 10000 + info.month * 100 + info.day));
  const int amp = sx(4 + static_cast<int>(hash % 5));
  const int freq10 = 15 + static_cast<int>((hash >> 8) % 26);  // 1.5〜4.0 周期（×10）
  const int phase = static_cast<int>((hash >> 16) % 64);

  // 波の縁（水面 ± 振幅）だけ列ごとに描き、その下は横一杯の矩形でまとめて塗る
  const int fringeBottom = std::min(H, water + amp + 1);
  for (int x = 0; x < W; x++) {
    const int idx = (x * freq10 * 64 / (10 * W) + phase) & 63;
    const int yl = std::clamp(water + amp * SIN64[idx] / 1024, 0, fringeBottom);
    const int ditherEnd = std::clamp(monthTop, yl, fringeBottom);
    if (ditherEnd > yl) renderer.fillRectDither(x, yl, 1, ditherEnd - yl, Color::DarkGray);
    if (fringeBottom > ditherEnd) renderer.fillRect(x, ditherEnd, 1, fringeBottom - ditherEnd, true);
  }
  if (fringeBottom < H) {
    const int bandEnd = std::clamp(monthTop, fringeBottom, H);
    if (bandEnd > fringeBottom) renderer.fillRectDither(0, fringeBottom, W, bandEnd - fringeBottom, Color::DarkGray);
    if (bandEnd < H) renderer.fillRect(0, bandEnd, W, H - bandEnd, true);
  }

  // 右端の目盛り（月の境）。水中では白、水上では黒。
  const int rulerX = W - sx(44);
  const int tickH = std::max(1, sy(2));
  int start = 0;
  for (int m = 1; m <= 12; m++) {
    // 1 月の目盛りは画面下端に重なるので、はみ出さないよう内側に寄せる
    const int ty = std::min(H - tickH, H - start * H / Y);
    const int len = sx((m - 1) % 3 == 0 ? 34 : 20);
    renderer.fillRect(W - len, ty - tickH / 2, len, tickH, ty < water);
    start += daysInMonth(info.year, m);
  }
  renderer.fillRect(rulerX, 0, 1, water, true);
  renderer.fillRect(rulerX, water, 1, H - water, false);

  // 見出し: 水面より上が広ければ上に黒、狭ければ水中に白
  const int headLeft = sx(40), headRight = rulerX - sx(24);
  if (water > sy(200)) {
    drawHeader(renderer, info, headLeft, headRight, sy(56), true);
  } else {
    drawHeader(renderer, info, headLeft, headRight, H - sy(130), false);
  }
}

// 03 年格子: 15 列の丸を左上から折り返して 1 日 1 点。黒＝過ぎた日、淡い＝これから、輪付き＝今日。
void DynamicWallpaper::renderDotGrid(const GfxRenderer& renderer, const YearInfo& info) {
  const int W = renderer.getScreenWidth();
  const int H = renderer.getScreenHeight();
  const auto sx = [W](int v) { return v * W / BASE_W; };
  const auto sy = [H](int v) { return v * H / BASE_H; };

  renderer.clearScreen();

  constexpr int COLS = 15;
  const int rows = (info.daysInYear + COLS - 1) / COLS;
  const int my = sy(166);
  // 横幅基準のピッチが縦に収まらない画面（X3 は横長寄り）では、縦の空きでピッチを制限する
  const int pitch = std::min(sx(25), (H - my - sy(30)) / (rows - 1));
  const int mx = (W - (COLS - 1) * pitch) / 2;
  const int r = std::max(2, pitch * 7 / 25);

  for (int i = 0; i < info.daysInYear; i++) {
    const int cx = mx + (i % COLS) * pitch;
    const int cy = my + (i / COLS) * pitch;
    if (i < info.done) {
      fillCircle(renderer, cx, cy, r, Color::Black);
    } else if (i == info.done) {
      fillCircle(renderer, cx, cy, r + std::max(3, pitch * 5 / 25), Color::Black);
      fillCircle(renderer, cx, cy, r + std::max(2, pitch * 3 / 25), Color::White);
      fillCircle(renderer, cx, cy, r, Color::Black);
    } else {
      fillCircle(renderer, cx, cy, r, Color::LightGray);
    }
  }

  drawHeader(renderer, info, mx - r, W - mx + r, sy(60), true);
}

// 05 年の升目: 年格子と同じ並びを升目で。黒＝過ぎた日、枠のみ＝これから、斜線＝今日。
void DynamicWallpaper::renderSquareGrid(const GfxRenderer& renderer, const YearInfo& info) {
  const int W = renderer.getScreenWidth();
  const int H = renderer.getScreenHeight();
  const auto sx = [W](int v) { return v * W / BASE_W; };
  const auto sy = [H](int v) { return v * H / BASE_H; };

  renderer.clearScreen();

  constexpr int COLS = 15;
  const int rows = (info.daysInYear + COLS - 1) / COLS;
  const int my = sy(160);
  // 横幅基準のピッチが縦に収まらない画面では、縦の空きでピッチを制限する
  const int pitch = std::min(sx(25), (H - my - sy(6)) / rows);
  const int gap = std::max(1, pitch * 3 / 25);
  const int cell = pitch - gap;
  const int mx = (W - (COLS * pitch - gap)) / 2;
  const int lw = std::max(1, W / 400);
  const int hatch = std::max(3, sx(6));

  for (int i = 0; i < info.daysInYear; i++) {
    const int x = mx + (i % COLS) * pitch;
    const int y = my + (i / COLS) * pitch;
    if (i < info.done) {
      renderer.fillRect(x, y, cell, cell, true);
    } else {
      renderer.drawRect(x, y, cell, cell, lw, true);
      if (i == info.done) {
        for (int yy = 0; yy < cell; yy++) {
          for (int xx = 0; xx < cell; xx++) {
            if ((xx + yy) % hatch < 2) renderer.drawPixel(x + xx, y + yy, true);
          }
        }
      }
    }
  }

  drawHeader(renderer, info, mx, W - mx, sy(60), true);
}
