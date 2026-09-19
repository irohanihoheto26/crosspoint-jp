#include "VegaTheme.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/icons/book_finished24.h"
#include "components/icons/book_reading24.h"
#include "components/icons/cover.h"
#include "fontIds.h"

// Vega: Lyra 派生。ホームを「角丸カード 1 枚＋角丸タイルのグリッド」にしたテーマ。
// ホーム以外（一覧・ヘッダ・キーボード等）は Lyra と同じ描画を使う
namespace {
constexpr int hPaddingInSelection = 8;
constexpr int mainMenuIconSize = 32;
constexpr int mainMenuColumns = 2;
constexpr int homeCardRadius = 12;
constexpr int homeCardPadding = 16;
constexpr int homeMenuTileHeight = 72;
constexpr int homeMenuMinTileHeight = 44;
constexpr int homeMenuGap = 12;
// タイルを 2 列にできる最小の列幅（アイコン 32 + 余白 + 6 文字の和文ラベル）
constexpr int homeMenuMinColumnWidth = 190;
constexpr int homeProgressBarHeight = 8;
// 表紙サムネイルの幅。SD から 1 回描いたあとは HomeActivity が保存した領域を復元するので、
// 幅だけをここに覚えておいて文字の位置を決める
int coverWidth = 0;
}  // namespace

void VegaTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                    const std::vector<ReadingProgress>& bookProgress, const int selectorIndex,
                                    bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                                    std::function<bool()> storeCoverBuffer) const {
  (void)bufferRestored;
  if (recentBooks.empty()) {
    drawEmptyRecents(renderer, rect);
    return;
  }

  // 角丸カード 1 枚に「表紙｜続きを読む・題名・著者・進捗バー」を収める。
  // 選択中は枠を太くし、「続きを読む」を黒いピルにして押せることを示す
  const int cardX = rect.x + VegaMetrics::values.contentSidePadding;
  const int cardY = rect.y;
  const int cardWidth = rect.width - 2 * VegaMetrics::values.contentSidePadding;
  const int cardHeight = rect.height;
  const int coverHeight = VegaMetrics::values.homeCoverHeight;
  const int coverX = cardX + homeCardPadding;
  const int coverY = cardY + (cardHeight - coverHeight) / 2;
  const int maxCoverWidth = cardWidth / 2;

  const RecentBook& book = recentBooks[0];
  const bool bookSelected = selectorIndex == 0;
  const ReadingProgress progress = bookProgress.empty() ? ReadingProgress{} : bookProgress[0];

  if (coverWidth == 0) {
    coverWidth = coverHeight * 3 / 5;
  }

  // 表紙は SD から 1 回だけ描き、以後は HomeActivity が保存した領域を復元する
  if (!coverRendered) {
    bool hasCover = false;
    if (!book.coverBmpPath.empty()) {
      const std::string coverBmpPath = UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight);
      FsFile file;
      if (Storage.openFileForRead("HOME", coverBmpPath, file)) {
        Bitmap bitmap(file);
        if (bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() > 0 && bitmap.getHeight() > 0) {
          // 横長の表紙はカードの半分までにして左右を切る（3 Covers と同じ扱い）
          float cropX = 0.0f;
          coverWidth = bitmap.getWidth();
          if (coverWidth > maxCoverWidth) {
            const float ratio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
            const float tileRatio = static_cast<float>(maxCoverWidth) / static_cast<float>(coverHeight);
            cropX = 1.0f - (tileRatio / ratio);
            coverWidth = maxCoverWidth;
          }
          renderer.drawBitmap(bitmap, coverX, coverY, coverWidth, coverHeight, cropX);
          hasCover = true;
        }
        file.close();
      }
    }
    if (!hasCover) {
      coverWidth = coverHeight * 3 / 5;
      renderer.fillRect(coverX, coverY + coverHeight / 3, coverWidth, 2 * coverHeight / 3, true);
      renderer.drawIcon(CoverIcon, coverX + 24, coverY + 24, 32, 32);
    }
    renderer.drawRect(coverX, coverY, coverWidth, coverHeight, true);

    coverBufferStored = storeCoverBuffer();
    coverRendered = coverBufferStored;  // Only consider it rendered if we successfully stored the buffer
  }

  // カードの枠（選択中は 3px）
  renderer.drawRoundedRect(cardX, cardY, cardWidth, cardHeight, bookSelected ? 3 : 1, homeCardRadius, true);

  const int textX = coverX + coverWidth + homeCardPadding;
  const int textRight = cardX + cardWidth - homeCardPadding;
  const int textWidth = textRight - textX;
  if (textWidth < 40) {
    return;  // 横向きで表紙列が極端に狭いときは文字を諦める
  }

  const int smallLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int titleLineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int authorLineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  int y = coverY;

  // 「続きを読む」ラベル。選択中は黒いピルに白抜き
  {
    constexpr int pillPadX = 10;
    constexpr int pillPadY = 3;
    const int pillHeight = smallLineHeight + pillPadY * 2;
    // 訳語が長い言語でもピルがカードの右枠を越えないよう、文字列側を省略する
    const std::string label = renderer.truncatedText(SMALL_FONT_ID, tr(STR_CONTINUE_READING), textWidth - pillPadX * 2);
    const int labelWidth = renderer.getTextWidth(SMALL_FONT_ID, label.c_str());
    if (bookSelected) {
      renderer.fillRoundedRect(textX, y, labelWidth + pillPadX * 2, pillHeight, pillHeight / 2, Color::Black);
      renderer.drawText(SMALL_FONT_ID, textX + pillPadX, y + pillPadY, label.c_str(), false);
    } else {
      renderer.drawText(SMALL_FONT_ID, textX + pillPadX, y + pillPadY, label.c_str(), true);
    }
    y += pillHeight + 12;
  }

  // 進捗行はカード下端に揃える。題名・著者はその上に収まる行数だけ描く
  const int progressRowHeight = std::max(smallLineHeight, 24);
  const int progressY = coverY + coverHeight - progressRowHeight;
  const bool showProgressRow =
      progress.hasPercent() || progress.status == ReadingStatus::Reading || progress.status == ReadingStatus::Finished;
  const int textBottom = showProgressRow ? progressY - 8 : coverY + coverHeight;

  // 題名（太字、最大 3 行）
  const int authorHeight = book.author.empty() ? 0 : authorLineHeight + 4;
  int maxTitleLines = (textBottom - y - authorHeight) / titleLineHeight;
  maxTitleLines = std::max(1, std::min(3, maxTitleLines));
  const auto titleLines =
      renderer.wrappedText(UI_12_FONT_ID, book.title.c_str(), textWidth, maxTitleLines, EpdFontFamily::BOLD);
  for (const auto& line : titleLines) {
    renderer.drawText(UI_12_FONT_ID, textX, y, line.c_str(), true, EpdFontFamily::BOLD);
    y += titleLineHeight;
  }
  if (!book.author.empty() && y + authorHeight <= textBottom) {
    y += 4;
    const auto author = renderer.truncatedText(UI_10_FONT_ID, book.author.c_str(), textWidth);
    renderer.drawText(UI_10_FONT_ID, textX, y, author.c_str(), true);
  }

  if (!showProgressRow) {
    return;
  }

  // 進捗行: [バー] 42%（読了は「読了」）。進捗率が無い旧キャッシュは状態アイコンだけ
  if (progress.hasPercent()) {
    char percentText[8];
    snprintf(percentText, sizeof(percentText), "%d%%", progress.percent);
    const char* label = (progress.status == ReadingStatus::Finished) ? tr(STR_READ_FINISHED) : percentText;
    const int labelWidth = renderer.getTextWidth(SMALL_FONT_ID, label);
    const int barWidth = textWidth - labelWidth - 10;
    const int labelY = progressY + (progressRowHeight - smallLineHeight) / 2;
    if (barWidth > 20) {
      drawThinProgressBar(
          renderer,
          Rect{textX, progressY + (progressRowHeight - homeProgressBarHeight) / 2, barWidth, homeProgressBarHeight},
          progress.percent, homeProgressBarHeight / 2);
    }
    renderer.drawText(SMALL_FONT_ID, textRight - labelWidth, labelY, label, true);
  } else {
    constexpr int readingStatusIconSize = 24;
    const uint8_t* iconBitmap = (progress.status == ReadingStatus::Finished) ? BookFinished24Icon : BookReading24Icon;
    renderer.drawIcon(iconBitmap, textX, progressY + (progressRowHeight - readingStatusIconSize) / 2,
                      readingStatusIconSize, readingStatusIconSize);
  }
}

void VegaTheme::drawEmptyRecents(const GfxRenderer& renderer, const Rect rect) const {
  // 本が無いときも同じ大きさの角丸カードを置き、レイアウトが跳ねないようにする
  const int cardX = rect.x + VegaMetrics::values.contentSidePadding;
  const int cardWidth = rect.width - 2 * VegaMetrics::values.contentSidePadding;
  renderer.drawRoundedRect(cardX, rect.y, cardWidth, rect.height, 1, homeCardRadius, true);

  const int titleLineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int subLineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int blockHeight = titleLineHeight + 6 + subLineHeight;
  int y = rect.y + (rect.height - blockHeight) / 2;
  const char* title = tr(STR_NO_OPEN_BOOK);
  const char* sub = tr(STR_START_READING);
  renderer.drawText(UI_12_FONT_ID,
                    cardX + (cardWidth - renderer.getTextWidth(UI_12_FONT_ID, title, EpdFontFamily::BOLD)) / 2, y,
                    title, true, EpdFontFamily::BOLD);
  y += titleLineHeight + 6;
  renderer.drawText(UI_10_FONT_ID, cardX + (cardWidth - renderer.getTextWidth(UI_10_FONT_ID, sub)) / 2, y, sub, true);
}

void VegaTheme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                               const std::function<std::string(int index)>& buttonLabel,
                               const std::function<UIIcon(int index)>& rowIcon) const {
  if (buttonCount <= 0) return;

  // 角丸タイルのグリッド。幅が足りれば 2 列、足りなければ 1 列（横向きの右カラムなど）。
  // 選択中のタイルは黒く塗って文字とアイコンを白抜きにする
  const int gridX = rect.x + VegaMetrics::values.contentSidePadding;
  const int gridWidth = rect.width - 2 * VegaMetrics::values.contentSidePadding;
  int columns = (gridWidth + homeMenuGap) / (homeMenuMinColumnWidth + homeMenuGap);
  columns = std::max(1, std::min(mainMenuColumns, columns));
  const int rows = (buttonCount + columns - 1) / columns;

  // 高さが足りないとき（横向き）はタイルを縮めて全行を収める
  int tileHeight = homeMenuTileHeight;
  if (rect.height > 0) {
    const int fitHeight = (rect.height - (rows - 1) * homeMenuGap) / rows;
    tileHeight = std::max(homeMenuMinTileHeight, std::min(tileHeight, fitHeight));
  }
  const int tileWidth = (gridWidth - (columns - 1) * homeMenuGap) / columns;

  // UI text is rendered via CJK UI bitmap font (20px) even for Latin characters.
  // The glyph's visual center sits (ascender - 6)px below textY, so center that in the tile.
  const int ascender = renderer.getFontAscenderSize(UI_12_FONT_ID);

  for (int i = 0; i < buttonCount; ++i) {
    const int row = i / columns;
    const int col = i % columns;
    // 2 列で最後が 1 つ余るときは幅いっぱいに広げて空きを作らない
    const bool spanAll = columns > 1 && i == buttonCount - 1 && col == 0;
    const Rect tile{gridX + col * (tileWidth + homeMenuGap), rect.y + row * (tileHeight + homeMenuGap),
                    spanAll ? gridWidth : tileWidth, tileHeight};
    const bool selected = selectedIndex == i;

    if (selected) {
      renderer.fillRoundedRect(tile.x, tile.y, tile.width, tile.height, homeCardRadius, Color::Black);
    } else {
      renderer.drawRoundedRect(tile.x, tile.y, tile.width, tile.height, 1, homeCardRadius, true);
    }

    const std::string labelStr = buttonLabel(i);
    const uint8_t* iconBitmap = rowIcon ? iconForName(rowIcon(i), mainMenuIconSize) : nullptr;
    const int iconAdvance = iconBitmap ? mainMenuIconSize + hPaddingInSelection + 2 : 0;
    const int maxLabelWidth = tile.width - 2 * homeCardPadding - iconAdvance;
    const std::string label = renderer.truncatedText(UI_12_FONT_ID, labelStr.c_str(), maxLabelWidth);
    const int labelWidth = renderer.getTextWidth(UI_12_FONT_ID, label.c_str());

    // 複数列ではアイコン＋ラベルをタイル中央に、1 列（横向き）では左揃えにする
    int contentX = tile.x + homeCardPadding;
    if (columns > 1) {
      contentX = tile.x + std::max(homeCardPadding, (tile.width - iconAdvance - labelWidth) / 2);
    }
    if (iconBitmap) {
      renderer.drawIcon(iconBitmap, contentX, tile.y + (tile.height - mainMenuIconSize) / 2, mainMenuIconSize,
                        mainMenuIconSize, !selected);
    }
    const int textY = tile.y + tile.height / 2 - ascender + 6;
    renderer.drawText(UI_12_FONT_ID, contentX + iconAdvance, textY, label.c_str(), !selected);
  }
}
