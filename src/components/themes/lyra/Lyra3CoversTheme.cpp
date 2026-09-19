#include "Lyra3CoversTheme.h"

#include <GfxRenderer.h>
#include <HalStorage.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "../HintOrientationScope.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/icons/book_finished24.h"
#include "components/icons/book_reading24.h"
#include "components/icons/cover.h"
#include "fontIds.h"

// Internal constants
namespace {
constexpr int hPaddingInSelection = 8;
constexpr int cornerRadius = 6;
}  // namespace

int Lyra3CoversTheme::getHomeRecentBooksCount(const GfxRenderer& renderer) const {
  return HintOrientationScope::isLandscape(renderer.getOrientation()) ? 2
                                                                      : Lyra3CoversMetrics::values.homeRecentBooksCount;
}

void Lyra3CoversTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                           const std::vector<ReadingProgress>& bookProgress, const int selectorIndex,
                                           bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                                           std::function<bool()> storeCoverBuffer) const {
  const int coverCount = getHomeRecentBooksCount(renderer);
  const int tileWidth = (rect.width - 2 * Lyra3CoversMetrics::values.contentSidePadding) / coverCount;
  const int tileY = rect.y;
  const bool hasContinueReading = !recentBooks.empty();

  // Draw book card regardless, fill with message based on `hasContinueReading`
  // Draw cover image as background if available (inside the box)
  // Only load from SD on first render, then use stored buffer
  if (hasContinueReading) {
    if (!coverRendered) {
      for (int i = 0; i < std::min(static_cast<int>(recentBooks.size()), coverCount); i++) {
        std::string coverPath = recentBooks[i].coverBmpPath;
        bool hasCover = true;
        int tileX = rect.x + Lyra3CoversMetrics::values.contentSidePadding + tileWidth * i;
        if (coverPath.empty()) {
          hasCover = false;
        } else {
          const std::string coverBmpPath =
              UITheme::getCoverThumbPath(coverPath, Lyra3CoversMetrics::values.homeCoverHeight);

          // First time: load cover from SD and render
          FsFile file;
          if (Storage.openFileForRead("HOME", coverBmpPath, file)) {
            Bitmap bitmap(file);
            if (bitmap.parseHeaders() == BmpReaderError::Ok) {
              float coverHeight = static_cast<float>(bitmap.getHeight());
              float coverWidth = static_cast<float>(bitmap.getWidth());
              float ratio = coverWidth / coverHeight;
              const float tileRatio = static_cast<float>(tileWidth - 2 * hPaddingInSelection) /
                                      static_cast<float>(Lyra3CoversMetrics::values.homeCoverHeight);
              float cropX = 1.0f - (tileRatio / ratio);

              renderer.drawBitmap(bitmap, tileX + hPaddingInSelection, tileY + hPaddingInSelection,
                                  tileWidth - 2 * hPaddingInSelection, Lyra3CoversMetrics::values.homeCoverHeight,
                                  cropX);
            } else {
              hasCover = false;
            }
            file.close();
          }
        }
        // Draw either way
        renderer.drawRect(tileX + hPaddingInSelection, tileY + hPaddingInSelection, tileWidth - 2 * hPaddingInSelection,
                          Lyra3CoversMetrics::values.homeCoverHeight, true);

        if (!hasCover) {
          // Render empty cover
          renderer.fillRect(tileX + hPaddingInSelection,
                            tileY + hPaddingInSelection + (Lyra3CoversMetrics::values.homeCoverHeight / 3),
                            tileWidth - 2 * hPaddingInSelection, 2 * Lyra3CoversMetrics::values.homeCoverHeight / 3,
                            true);
          renderer.drawIcon(CoverIcon, tileX + hPaddingInSelection + 24, tileY + hPaddingInSelection + 24, 32, 32);
        }
      }

      coverBufferStored = storeCoverBuffer();
      coverRendered = coverBufferStored;  // Only consider it rendered if we successfully stored the buffer
    }

    for (int i = 0; i < std::min(static_cast<int>(recentBooks.size()), coverCount); i++) {
      bool bookSelected = (selectorIndex == i);

      int tileX = rect.x + Lyra3CoversMetrics::values.contentSidePadding + tileWidth * i;

      const int maxLineWidth = tileWidth - 2 * hPaddingInSelection;

      // 題名は 1 行に省略する。複数行にすると進捗バーの行が下のメニューに食い込む
      const std::string titleLine = renderer.truncatedText(SMALL_FONT_ID, recentBooks[i].title.c_str(), maxLineWidth);

      constexpr int readingStatusIconSize = 24;
      constexpr int readingStatusIconTopMargin = 4;
      const bool hasProgress = i < static_cast<int>(bookProgress.size());
      const ReadingStatus status = hasProgress ? bookProgress[i].status : ReadingStatus::Unread;
      const bool hasReadingStatusIcon = status == ReadingStatus::Reading || status == ReadingStatus::Finished;
      const bool hasPercent = hasProgress && bookProgress[i].hasPercent();

      const int titleLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
      const int dynamicBlockHeight = titleLineHeight;
      const int readingStatusBlockHeight =
          hasReadingStatusIcon ? (readingStatusIconSize + readingStatusIconTopMargin) : 0;
      // Add a little padding below the text inside the selection box just like the top padding (5 + hPaddingSelection)
      const int dynamicTitleBoxHeight = dynamicBlockHeight + readingStatusBlockHeight + hPaddingInSelection + 5;

      if (bookSelected) {
        // Draw selection box
        renderer.fillRoundedRect(tileX, tileY, tileWidth, hPaddingInSelection, cornerRadius, true, true, false, false,
                                 Color::LightGray);
        renderer.fillRectDither(tileX, tileY + hPaddingInSelection, hPaddingInSelection,
                                Lyra3CoversMetrics::values.homeCoverHeight, Color::LightGray);
        renderer.fillRectDither(tileX + tileWidth - hPaddingInSelection, tileY + hPaddingInSelection,
                                hPaddingInSelection, Lyra3CoversMetrics::values.homeCoverHeight, Color::LightGray);
        renderer.fillRoundedRect(tileX, tileY + Lyra3CoversMetrics::values.homeCoverHeight + hPaddingInSelection,
                                 tileWidth, dynamicTitleBoxHeight, cornerRadius, false, false, true, true,
                                 Color::LightGray);
      }

      int currentY = tileY + Lyra3CoversMetrics::values.homeCoverHeight + hPaddingInSelection + 5;
      renderer.drawText(SMALL_FONT_ID, tileX + hPaddingInSelection, currentY, titleLine.c_str(), true);
      currentY += titleLineHeight;
      if (hasReadingStatusIcon) {
        currentY += readingStatusIconTopMargin;
        const uint8_t* iconBitmap = (status == ReadingStatus::Finished) ? BookFinished24Icon : BookReading24Icon;
        renderer.drawIcon(iconBitmap, tileX + hPaddingInSelection, currentY, readingStatusIconSize,
                          readingStatusIconSize);
        // アイコンの右に進捗バーと百分率（読了は 100% のバーだけで十分なので数字は省く）
        if (hasPercent) {
          const int barX = tileX + hPaddingInSelection + readingStatusIconSize + 6;
          const int barRight = tileX + tileWidth - hPaddingInSelection;
          constexpr int barHeight = 8;
          char percentText[8];
          snprintf(percentText, sizeof(percentText), "%d%%", bookProgress[i].percent);
          const int percentWidth =
              (status == ReadingStatus::Finished) ? 0 : renderer.getTextWidth(SMALL_FONT_ID, percentText) + 6;
          const int barWidth = barRight - barX - percentWidth;
          if (barWidth > 20) {
            const int barY = currentY + (readingStatusIconSize - barHeight) / 2;
            drawThinProgressBar(renderer, Rect{barX, barY, barWidth, barHeight}, bookProgress[i].percent, 4);
            if (percentWidth > 0) {
              const int textY = currentY + (readingStatusIconSize - renderer.getLineHeight(SMALL_FONT_ID)) / 2;
              renderer.drawText(SMALL_FONT_ID, barX + barWidth + 6, textY, percentText, true);
            }
          }
        }
      }
    }
  } else {
    drawEmptyRecents(renderer, rect);
  }
}
