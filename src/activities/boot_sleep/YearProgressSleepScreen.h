#pragma once

#include <cstdint>
#include <ctime>

class GfxRenderer;
enum Color : uint8_t;

// 「年の進み」スリープ画面
//
// 年月日だけで決まる壁紙。今年の何割が過ぎたかを図形で示し、残り日数と経過率を
// 文字でも入れる。時刻は使わないので、ロックした瞬間の日付で絵が確定する。
// 描画はすべて既存のフレームバッファへの直接描画で、追加のヒープ確保はない。
class YearProgressSleepScreen {
 public:
  // style は CrossPointSettings::YEAR_PROGRESS_STYLE の値。
  // 呼び出し側で clearScreen 済みである必要はない（内部で白に塗る）。
  // displayBuffer は呼ばない（呼び出し側でカレンダー等を重ねてから表示する）。
  static void render(GfxRenderer& renderer, uint8_t style, const struct tm& date);

 private:
  struct YearInfo {
    int year;
    int month;       // 1-12
    int day;         // 1-31
    int dayOfYear;   // 1 始まり（1 月 1 日 = 1）
    int daysInYear;  // 365 or 366
    int done;        // 過ぎた日数（昨日まで）= dayOfYear - 1
  };
  static YearInfo computeYearInfo(const struct tm& date);

  static void renderWaterLevel(GfxRenderer& renderer, const YearInfo& info);
  static void renderDotGrid(GfxRenderer& renderer, const YearInfo& info);
  static void renderSquareGrid(GfxRenderer& renderer, const YearInfo& info);

  // 見出し（残り日数・経過率・罫線）。yTop は大きな数字の上端。
  static void drawHeader(GfxRenderer& renderer, const YearInfo& info, int left, int right, int yTop, bool black);
  static int drawTracked(GfxRenderer& renderer, int fontId, int x, int top, const char* text, int tracking, bool draw,
                         bool black);
  static void fillCircle(GfxRenderer& renderer, int cx, int cy, int r, Color color);
};
