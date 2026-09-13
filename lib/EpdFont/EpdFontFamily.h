#pragma once
#include "EpdFont.h"

class EpdFontFamily {
 public:
  // ビットフラグ。下位 2 ビット（BOLD / ITALIC）だけが書体の選択に使われ、
  // UNDERLINE / SUPERSCRIPT / SUBSCRIPT は getFont() では無視される装飾ビット。
  enum Style : uint8_t {
    REGULAR = 0,
    BOLD = 1,
    ITALIC = 2,
    BOLD_ITALIC = 3,
    UNDERLINE = 4,
    SUPERSCRIPT = 8,   // <sup>: 小さいフォントで行の上端に寄せて描く
    SUBSCRIPT = 16,    // <sub>: 小さいフォントで行の下端に寄せて描く
    SCRIPT_MASK = 24,  // SUPERSCRIPT | SUBSCRIPT
  };

  // 書体（Regular / Bold / Italic / BoldItalic）の選択に使うビット。
  // SD カードフォントの advance テーブルはこの 4 つでしか引けないので、
  // 装飾ビットを落としてから添字にすること。
  static constexpr uint8_t FONT_SELECT_MASK = BOLD | ITALIC;

  explicit EpdFontFamily(const EpdFont* regular, const EpdFont* bold = nullptr, const EpdFont* italic = nullptr,
                         const EpdFont* boldItalic = nullptr)
      : regular(regular), bold(bold), italic(italic), boldItalic(boldItalic) {}
  ~EpdFontFamily() = default;
  void getTextDimensions(const char* string, int* w, int* h, Style style = REGULAR) const;
  const EpdFontData* getData(Style style = REGULAR) const;
  const EpdGlyph* getGlyph(uint32_t cp, Style style = REGULAR) const;
  int8_t getKerning(uint32_t leftCp, uint32_t rightCp, Style style = REGULAR) const;
  uint32_t applyLigatures(uint32_t cp, const char*& text, Style style = REGULAR) const;

 private:
  const EpdFont* regular;
  const EpdFont* bold;
  const EpdFont* italic;
  const EpdFont* boldItalic;

  const EpdFont* getFont(Style style) const;
};
