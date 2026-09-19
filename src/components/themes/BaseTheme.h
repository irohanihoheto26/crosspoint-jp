#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "ReadingStatusHelper.h"

class GfxRenderer;
struct RecentBook;

struct Rect {
  int x;
  int y;
  int width;
  int height;

  explicit Rect(int x = 0, int y = 0, int width = 0, int height = 0) : x(x), y(y), width(width), height(height) {}
};

struct TabInfo {
  const char* label;
  bool selected;
};

// ボタンヒントが占める領域ぶんの余白（論理座標）。ヒントは前面ボタンの物理位置に
// 合わせて描かれるため、向きによって上下左右のどこを空けるべきかが変わる。
// 縦持ち: 下端（反転時は上端）。横向き: ボタンのある短辺側（CW は左、CCW は右）。
struct ButtonHintInsets {
  int top = 0;
  int bottom = 0;
  int left = 0;
  int right = 0;
};

struct ThemeMetrics {
  int batteryWidth;
  int batteryHeight;

  int topPadding;
  int batteryBarHeight;
  int headerHeight;
  int verticalSpacing;

  int contentSidePadding;
  int listRowHeight;
  int listWithSubtitleRowHeight;
  int menuRowHeight;
  int menuSpacing;

  int tabSpacing;
  int tabBarHeight;

  int scrollBarWidth;
  int scrollBarRightOffset;

  int homeTopPadding;
  int homeCoverHeight;
  int homeCoverTileHeight;
  int homeRecentBooksCount;
  bool homeContinueReadingInMenu;
  int homeMenuTopOffset;

  int buttonHintsHeight;
  int sideButtonHintsWidth;

  int progressBarHeight;
  int progressBarMarginTop;
  int statusBarHorizontalMargin;
  int statusBarVerticalMargin;

  int keyboardKeyWidth;
  int keyboardKeyHeight;
  int keyboardKeySpacing;
  int keyboardBottomKeyHeight;
  int keyboardBottomKeySpacing;
  bool keyboardBottomAligned;
  bool keyboardCenteredText;
  int keyboardVerticalOffset;
  int keyboardTextFieldWidthPercent;
  int keyboardWidthPercent;
  int keyboardKeyCornerRadius;
};

enum UIIcon {
  Folder,
  Text,
  Image,
  Book,
  File,
  Recent,
  Settings,
  Transfer,
  Library,
  Wifi,
  Hotspot,
  BookUnread,
  BookReading,
  BookFinished
};

enum class KeyboardKeyType { Normal, Shift, Mode, Space, Del, Ok, Disabled };

// テーマの共通インターフェース。各テーマ（Lyra 系）がこれを継承して描画を実装する。
// 純粋仮想の関数はテーマごとに見た目が違うもの、実装を持つ関数は全テーマ共通のもの
class BaseTheme {
 public:
  virtual ~BaseTheme() = default;

  // Component drawing methods
  virtual void drawProgressBar(const GfxRenderer& renderer, Rect rect, size_t current, size_t total) const;
  virtual void drawBatteryLeft(const GfxRenderer& renderer, Rect rect,
                               bool showPercentage = true) const = 0;  // Left aligned (reader mode)
  virtual void drawBatteryRight(const GfxRenderer& renderer, Rect rect,
                                bool showPercentage = true) const = 0;  // Right aligned (UI headers)
  // drawButtonHints() が占める領域を、コンテンツ側が避けるための余白を返す。
  // buttonHintsHeight + verticalSpacing ぶんを、現在の向きに応じた辺に割り当てる。
  ButtonHintInsets getButtonHintInsets(const GfxRenderer& renderer) const;
  virtual void drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                               const char* btn4) const = 0;
  virtual void drawSideButtonHints(const GfxRenderer& renderer, const char* topBtn, const char* bottomBtn) const = 0;
  virtual void drawList(const GfxRenderer& renderer, Rect rect, int itemCount, int selectedIndex,
                        const std::function<std::string(int index)>& rowTitle,
                        const std::function<std::string(int index)>& rowSubtitle = nullptr,
                        const std::function<UIIcon(int index)>& rowIcon = nullptr,
                        const std::function<std::string(int index)>& rowValue = nullptr, bool highlightValue = false,
                        const std::function<bool(int index)>& rowDimmed = nullptr) const = 0;
  virtual void drawHeader(const GfxRenderer& renderer, Rect rect, const char* title,
                          const char* subtitle = nullptr) const = 0;
  virtual void drawSubHeader(const GfxRenderer& renderer, Rect rect, const char* label,
                             const char* rightLabel = nullptr) const = 0;
  virtual void drawTabBar(const GfxRenderer& renderer, Rect rect, const std::vector<TabInfo>& tabs,
                          bool selected) const = 0;
  // Home に並べる「最近の本」の枚数。テーマ既定は metrics.homeRecentBooksCount だが、
  // 横向きで幅が足りないテーマは少なくできる（Lyra 3 Covers は横向きで 2 枚）。
  virtual int getHomeRecentBooksCount(const GfxRenderer& renderer) const;
  // bookProgress は recentBooks と同じ長さ（各本の読書状態と進捗率）
  virtual void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                   const std::vector<ReadingProgress>& bookProgress, const int selectorIndex,
                                   bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                                   std::function<bool()> storeCoverBuffer) const = 0;
  virtual void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                              const std::function<std::string(int index)>& buttonLabel,
                              const std::function<UIIcon(int index)>& rowIcon) const = 0;
  virtual Rect drawPopup(const GfxRenderer& renderer, const char* message) const = 0;
  virtual void fillPopupProgress(const GfxRenderer& renderer, const Rect& layout, const int progress) const = 0;
  virtual void drawStatusBar(GfxRenderer& renderer, const float bookProgress, const int currentPage,
                             const int pageCount, std::string title, const int paddingBottom = 0,
                             const int textYOffset = 0, const bool rtlProgress = false) const;
  virtual void drawHelpText(const GfxRenderer& renderer, Rect rect, const char* label) const;
  virtual void drawTextField(const GfxRenderer& renderer, Rect rect, const int textWidth, bool cursorMode = false,
                             int contentStartX = 0, int contentWidth = 0) const;
  virtual void drawKeyboardKey(const GfxRenderer& renderer, Rect rect, const char* label, const bool isSelected,
                               const char* secondaryLabel = nullptr, KeyboardKeyType keyType = KeyboardKeyType::Normal,
                               bool inactiveSelection = false) const;
  virtual bool showsFileIcons() const { return false; }

  // 細い進捗バー（ホーム画面の「最近の本」用）。外枠を描き、percent ぶんを塗る。
  // cornerRadius = 0 で角丸なし。inverted = true は黒地に白で描く（選択中のカード内など）
  static void drawThinProgressBar(const GfxRenderer& renderer, Rect rect, int percent, int cornerRadius,
                                  bool inverted = false);

  // Shared constants and helpers for battery drawing (used by all themes)
  static constexpr int batteryPercentSpacing = 4;
  static void drawBatteryOutline(const GfxRenderer& renderer, int x, int y, int battWidth, int rectHeight);
  static void drawBatteryLightningBolt(const GfxRenderer& renderer, int boltX, int boltY);
};
