#include "BaseTheme.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>

#include "I18n.h"
#include "components/UITheme.h"
#include "fontIds.h"

void BaseTheme::drawBatteryOutline(const GfxRenderer& renderer, int x, int y, int battWidth, int rectHeight) {
  // Top line
  renderer.drawLine(x + 1, y, x + battWidth - 3, y);
  // Bottom line
  renderer.drawLine(x + 1, y + rectHeight - 1, x + battWidth - 3, y + rectHeight - 1);
  // Left line
  renderer.drawLine(x, y + 1, x, y + rectHeight - 2);
  // Battery end
  renderer.drawLine(x + battWidth - 2, y + 1, x + battWidth - 2, y + rectHeight - 2);
  renderer.drawPixel(x + battWidth - 1, y + 3);
  renderer.drawPixel(x + battWidth - 1, y + rectHeight - 4);
  renderer.drawLine(x + battWidth - 0, y + 4, x + battWidth - 0, y + rectHeight - 5);
}

void BaseTheme::drawBatteryLightningBolt(const GfxRenderer& renderer, int boltX, int boltY) {
  // Draw lightning bolt (white/inverted on black fill for visibility)
  renderer.drawLine(boltX + 4, boltY + 0, boltX + 5, boltY + 0, false);
  renderer.drawLine(boltX + 3, boltY + 1, boltX + 4, boltY + 1, false);
  renderer.drawLine(boltX + 2, boltY + 2, boltX + 5, boltY + 2, false);
  renderer.drawLine(boltX + 3, boltY + 3, boltX + 4, boltY + 3, false);
  renderer.drawLine(boltX + 2, boltY + 4, boltX + 3, boltY + 4, false);
  renderer.drawLine(boltX + 1, boltY + 5, boltX + 4, boltY + 5, false);
  renderer.drawLine(boltX + 2, boltY + 6, boltX + 3, boltY + 6, false);
  renderer.drawLine(boltX + 1, boltY + 7, boltX + 2, boltY + 7, false);
}

void BaseTheme::drawProgressBar(const GfxRenderer& renderer, Rect rect, const size_t current,
                                const size_t total) const {
  if (total == 0) {
    return;
  }

  // Use 64-bit arithmetic to avoid overflow for large files
  const int percent = static_cast<int>((static_cast<uint64_t>(current) * 100) / total);

  LOG_DBG("UI", "Drawing progress bar: current=%u, total=%u, percent=%d", current, total, percent);
  // Draw outline
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height);

  // Draw filled portion
  const int fillWidth = (rect.width - 4) * percent / 100;
  if (fillWidth > 0) {
    renderer.fillRect(rect.x + 2, rect.y + 2, fillWidth, rect.height - 4);
  }

  // Draw percentage text centered below bar（画面ではなくバーの中央に揃える。横向きでは
  // バーがヒント帯を避けて置かれるので、画面中央とは一致しない）
  const std::string percentText = std::to_string(percent) + "%";
  const int textX = rect.x + (rect.width - renderer.getTextWidth(UI_10_FONT_ID, percentText.c_str())) / 2;
  renderer.drawText(UI_10_FONT_ID, textX, rect.y + rect.height + 15, percentText.c_str());
}

ButtonHintInsets BaseTheme::getButtonHintInsets(const GfxRenderer& renderer) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int reserved = metrics.buttonHintsHeight + metrics.verticalSpacing;
  ButtonHintInsets insets;
  switch (renderer.getOrientation()) {
    case GfxRenderer::Orientation::PortraitInverted:
      insets.top = reserved;
      break;
    case GfxRenderer::Orientation::LandscapeClockwise:
      // 縦持ちの下端（ボタン側）が左に来る
      insets.left = reserved;
      break;
    case GfxRenderer::Orientation::LandscapeCounterClockwise:
      insets.right = reserved;
      break;
    case GfxRenderer::Orientation::Portrait:
    default:
      insets.bottom = reserved;
      break;
  }
  return insets;
}

int BaseTheme::getHomeRecentBooksCount(const GfxRenderer& /*renderer*/) const {
  return UITheme::getInstance().getMetrics().homeRecentBooksCount;
}

void BaseTheme::drawThinProgressBar(const GfxRenderer& renderer, Rect rect, int percent, int cornerRadius,
                                    bool inverted) {
  if (rect.width <= 2 || rect.height <= 2) return;
  percent = std::max(0, std::min(100, percent));
  const bool ink = !inverted;
  if (cornerRadius > 0) {
    renderer.drawRoundedRect(rect.x, rect.y, rect.width, rect.height, 1, cornerRadius, ink);
  } else {
    renderer.drawRect(rect.x, rect.y, rect.width, rect.height, ink);
  }
  // 枠の内側 1px を空けて塗る。1% でも 1px は見えるように切り上げる
  const int innerWidth = rect.width - 4;
  const int fillWidth = (innerWidth * percent + 99) / 100;
  if (fillWidth > 0) {
    renderer.fillRect(rect.x + 2, rect.y + 2, std::min(fillWidth, innerWidth), rect.height - 4, ink);
  }
}

void BaseTheme::drawStatusBar(GfxRenderer& renderer, const float bookProgress, const int currentPage,
                              const int pageCount, std::string title, const int paddingBottom, const int textYOffset,
                              const bool rtlProgress) const {
  auto metrics = UITheme::getInstance().getMetrics();
  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                   &orientedMarginLeft);

  // Draw Progress Text
  const auto screenHeight = renderer.getScreenHeight();
  auto textY = screenHeight - UITheme::getInstance().getStatusBarHeight() - orientedMarginBottom - paddingBottom - 4;
  int progressTextWidth = 0;

  if (SETTINGS.statusBarBookProgressPercentage || SETTINGS.statusBarChapterPageCount) {
    // Right aligned text for progress counter
    char progressStr[32];

    if (SETTINGS.statusBarBookProgressPercentage && SETTINGS.statusBarChapterPageCount) {
      snprintf(progressStr, sizeof(progressStr), "%d/%d  %.0f%%", currentPage, pageCount, bookProgress);
    } else if (SETTINGS.statusBarBookProgressPercentage) {
      snprintf(progressStr, sizeof(progressStr), "%.0f%%", bookProgress);
    } else {
      snprintf(progressStr, sizeof(progressStr), "%d/%d", currentPage, pageCount);
    }

    progressTextWidth = renderer.getTextWidth(SMALL_FONT_ID, progressStr);
    renderer.drawText(
        SMALL_FONT_ID,
        renderer.getScreenWidth() - metrics.statusBarHorizontalMargin - orientedMarginRight - progressTextWidth, textY,
        progressStr);
  }

  // Draw Progress Bar
  if (SETTINGS.statusBarProgressBar != CrossPointSettings::STATUS_BAR_PROGRESS_BAR::HIDE_PROGRESS) {
    const int progressBarMaxWidth = renderer.getScreenWidth() - orientedMarginLeft - orientedMarginRight;
    const int progressBarY = renderer.getScreenHeight() - orientedMarginBottom -
                             ((SETTINGS.statusBarProgressBarThickness + 1) * 2) - paddingBottom;
    size_t progress;
    if (SETTINGS.statusBarProgressBar == CrossPointSettings::STATUS_BAR_PROGRESS_BAR::BOOK_PROGRESS) {
      progress = static_cast<size_t>(bookProgress);
    } else {
      // Chapter progress
      progress = (pageCount > 0) ? (static_cast<float>(currentPage) / pageCount) * 100 : 0;
    }
    const int barWidth = progressBarMaxWidth * progress / 100;
    if (rtlProgress) {
      // RTL progress bar for vertical text: fills from right to left
      const int barX = orientedMarginLeft + progressBarMaxWidth - barWidth;
      renderer.fillRect(barX, progressBarY, barWidth, ((SETTINGS.statusBarProgressBarThickness + 1) * 2), true);
    } else {
      renderer.fillRect(orientedMarginLeft, progressBarY, barWidth, ((SETTINGS.statusBarProgressBarThickness + 1) * 2),
                        true);
    }
  }

  // Draw Battery
  const bool showBatteryPercentage =
      SETTINGS.hideBatteryPercentage == CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_NEVER;
  if (SETTINGS.statusBarBattery) {
    GUI.drawBatteryLeft(renderer,
                        Rect{metrics.statusBarHorizontalMargin + orientedMarginLeft + 1, textY, metrics.batteryWidth,
                             metrics.batteryHeight},
                        showBatteryPercentage);
  }

  // Draw Title
  if (!title.empty()) {
    textY -= textYOffset;
    // Centered chapter title text
    // Page width minus existing content with 30px padding on each side
    const int rendererableScreenWidth =
        renderer.getScreenWidth() - (metrics.statusBarHorizontalMargin * 2) - orientedMarginLeft - orientedMarginRight;

    const int batterySize = SETTINGS.statusBarBattery ? (showBatteryPercentage ? 50 : 20) : 0;
    const int titleMarginLeft = batterySize + 30;
    const int titleMarginRight = progressTextWidth + 30;

    // Attempt to center title on the screen, but if title is too wide then later we will center it within the
    // available space.
    int titleMarginLeftAdjusted = std::max(titleMarginLeft, titleMarginRight);
    int availableTitleSpace = rendererableScreenWidth - 2 * titleMarginLeftAdjusted;

    int titleWidth;
    titleWidth = renderer.getTextWidth(SMALL_FONT_ID, title.c_str());
    if (titleWidth > availableTitleSpace) {
      // Not enough space to center on the screen, center it within the remaining space instead
      availableTitleSpace = rendererableScreenWidth - titleMarginLeft - titleMarginRight;
      titleMarginLeftAdjusted = titleMarginLeft;
    }
    if (titleWidth > availableTitleSpace) {
      title = renderer.truncatedText(SMALL_FONT_ID, title.c_str(), availableTitleSpace);
      titleWidth = renderer.getTextWidth(SMALL_FONT_ID, title.c_str());
    }

    renderer.drawText(SMALL_FONT_ID,
                      titleMarginLeftAdjusted + metrics.statusBarHorizontalMargin + orientedMarginLeft +
                          (availableTitleSpace - titleWidth) / 2,
                      textY, title.c_str());
  }
}

void BaseTheme::drawHelpText(const GfxRenderer& renderer, Rect rect, const char* label) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  auto truncatedLabel =
      renderer.truncatedText(SMALL_FONT_ID, label, rect.width - metrics.contentSidePadding * 2, EpdFontFamily::REGULAR);
  renderer.drawCenteredText(SMALL_FONT_ID, rect.y, truncatedLabel.c_str());
}

void BaseTheme::drawTextField(const GfxRenderer& renderer, Rect rect, const int textWidth, bool cursorMode,
                              int contentStartX, int contentWidth) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int lineY = rect.y + rect.height + lineHeight + metrics.verticalSpacing;
  const int thickness = cursorMode ? 3 : 1;
  if (contentWidth > 0) {
    renderer.drawLine(rect.x + contentStartX, lineY, rect.x + contentStartX + contentWidth, lineY, thickness, true);
  } else {
    const int hPadding = 6;
    const int lineW = textWidth + hPadding * 2;
    renderer.drawLine(rect.x + (rect.width - lineW) / 2, lineY, rect.x + (rect.width + lineW) / 2, lineY, thickness,
                      true);
  }
}

void BaseTheme::drawKeyboardKey(const GfxRenderer& renderer, Rect rect, const char* label, const bool isSelected,
                                const char* secondaryLabel, const KeyboardKeyType keyType,
                                const bool inactiveSelection) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int cr = metrics.keyboardKeyCornerRadius;

  if (isSelected) {
    if (inactiveSelection) {
      if (cr > 0) {
        renderer.fillRoundedRect(rect.x, rect.y, rect.width, rect.height, cr, Color::LightGray);
      } else {
        renderer.drawRect(rect.x, rect.y, rect.width, rect.height, 2, true);
      }
    } else if (keyType == KeyboardKeyType::Disabled) {
      if (cr > 0) {
        renderer.fillRoundedRect(rect.x, rect.y, rect.width, rect.height, cr, Color::LightGray);
      } else {
        renderer.fillRectDither(rect.x, rect.y, rect.width, rect.height, Color::LightGray);
      }
    } else {
      if (cr > 0) {
        renderer.fillRoundedRect(rect.x, rect.y, rect.width, rect.height, cr, Color::Black);
      } else {
        renderer.fillRect(rect.x, rect.y, rect.width, rect.height, true);
      }
    }
  } else if (keyType == KeyboardKeyType::Shift || keyType == KeyboardKeyType::Mode || keyType == KeyboardKeyType::Del ||
             keyType == KeyboardKeyType::Space || keyType == KeyboardKeyType::Ok ||
             keyType == KeyboardKeyType::Disabled) {
    if (cr > 0) {
      renderer.drawRoundedRect(rect.x, rect.y, rect.width, rect.height, 1, cr, true);
    } else {
      renderer.drawRect(rect.x, rect.y, rect.width, rect.height);
    }
  }

  const bool invert = isSelected && !inactiveSelection;

  if (keyType == KeyboardKeyType::Space) {
    const int lineHalfWidth = rect.width * 3 / 10;
    const int centerX = rect.x + rect.width / 2;
    const int lineY = rect.y + rect.height / 2 + 3;
    renderer.drawLine(centerX - lineHalfWidth, lineY, centerX + lineHalfWidth, lineY, 3, !invert);
    return;
  }

  if (keyType == KeyboardKeyType::Del) {
    const int centerX = rect.x + rect.width / 2;
    const int centerY = rect.y + rect.height / 2;
    const int arrowLen = rect.width / 4;
    const int arrowHead = arrowLen / 2;
    renderer.drawLine(centerX - arrowLen / 2, centerY, centerX + arrowLen / 2, centerY, 3, !invert);
    renderer.drawLine(centerX - arrowLen / 2, centerY, centerX - arrowLen / 2 + arrowHead, centerY - arrowHead, 3,
                      !invert);
    renderer.drawLine(centerX - arrowLen / 2, centerY, centerX - arrowLen / 2 + arrowHead, centerY + arrowHead, 3,
                      !invert);
    return;
  }

  const bool hasSecondary = secondaryLabel != nullptr && secondaryLabel[0] != '\0';
  const int itemWidth = renderer.getTextWidth(UI_12_FONT_ID, label);
  const int textX = rect.x + (rect.width - itemWidth) / 2;
  const int textY = rect.y + (rect.height - renderer.getLineHeight(UI_12_FONT_ID)) / 2;

  renderer.drawText(UI_12_FONT_ID, textX, textY, label, !invert);

  if (hasSecondary) {
    const int secWidth = renderer.getTextWidth(SMALL_FONT_ID, secondaryLabel);
    renderer.drawText(SMALL_FONT_ID, rect.x + rect.width - secWidth - 1, rect.y, secondaryLabel, !invert);
  }
}
