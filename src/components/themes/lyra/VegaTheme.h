#pragma once

#include "components/themes/lyra/LyraTheme.h"

class GfxRenderer;

// Vega は Lyra のメトリクスを引き継ぎ、ホームのカードだけ表紙の周りに余白を取る
namespace VegaMetrics {
constexpr ThemeMetrics values = [] {
  ThemeMetrics v = LyraMetrics::values;
  v.homeCoverTileHeight = 258;  // 表紙 226 + カード内余白 16×2
  return v;
}();
}  // namespace VegaMetrics

// Lyra 派生テーマ。ホーム画面を角丸カード（表紙・続きを読む・題名・著者・進捗バー）と
// 2 列の角丸タイル（選択中は黒塗りに白抜き）で構成する
class VegaTheme : public LyraTheme {
 public:
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           const std::vector<ReadingProgress>& bookProgress, const int selectorIndex,
                           bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           std::function<bool()> storeCoverBuffer) const override;
  void drawEmptyRecents(const GfxRenderer& renderer, Rect rect) const override;
  void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                      const std::function<std::string(int index)>& buttonLabel,
                      const std::function<UIIcon(int index)>& rowIcon) const override;
};
