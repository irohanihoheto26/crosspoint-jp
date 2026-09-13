#include "ParsedText.h"

#include <GfxRenderer.h>
#include <Utf8.h>
#include <VerticalTextUtils.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iterator>
#include <limits>
#include <vector>

#include "InlineImage.h"
#include "Kinsoku.h"
#include "SectionBuildPerf.h"
#include "hyphenation/Hyphenator.h"

namespace {

// Soft hyphen byte pattern used throughout EPUBs (UTF-8 for U+00AD).
constexpr char SOFT_HYPHEN_UTF8[] = "\xC2\xAD";
constexpr size_t SOFT_HYPHEN_BYTES = 2;

bool containsSoftHyphen(const std::string& word) { return word.find(SOFT_HYPHEN_UTF8) != std::string::npos; }

// Removes every soft hyphen in-place so rendered glyphs match measured widths.
void stripSoftHyphensInPlace(std::string& word) {
  size_t pos = 0;
  while ((pos = word.find(SOFT_HYPHEN_UTF8, pos)) != std::string::npos) {
    word.erase(pos, SOFT_HYPHEN_BYTES);
  }
}

// Returns the advance width for a word while ignoring soft hyphen glyphs and optionally appending a visible hyphen.
// Uses advance width (sum of glyph advances + kerning) rather than bounding box width so that italic glyph overhangs
// don't inflate inter-word spacing.
// <sup> / <sub> の語は小さいフォントで描かれるので、幅もそのフォントで測る。
// ここで本文フォントの幅を使うと、描画より広く見積もって前後に隙間が空く。
int scriptAwareFontId(const int fontId, const EpdFontFamily::Style style) {
  if ((style & EpdFontFamily::SCRIPT_MASK) != 0 && TextBlock::smallFontId != 0) {
    return TextBlock::smallFontId;
  }
  return fontId;
}

uint16_t measureWordWidth(const GfxRenderer& renderer, const int blockFontId, const std::string& word,
                          const EpdFontFamily::Style style, const bool appendHyphen = false) {
  const int fontId = scriptAwareFontId(blockFontId, style);
  if (word.size() == 1 && word[0] == ' ' && !appendHyphen) {
    return renderer.getSpaceWidth(fontId, style);
  }
  const bool hasSoftHyphen = containsSoftHyphen(word);
  if (!hasSoftHyphen && !appendHyphen) {
    return renderer.getTextAdvanceX(fontId, word.c_str(), style);
  }

  std::string sanitized = word;
  if (hasSoftHyphen) {
    stripSoftHyphensInPlace(sanitized);
  }
  if (appendHyphen) {
    sanitized.push_back('-');
  }
  return renderer.getTextAdvanceX(fontId, sanitized.c_str(), style);
}

// このブロックで実際に使われているフォントスタイルのビット集合を返す。
// SDカードフォントの advance テーブルを、使わないスタイルのぶんまで
// 構築してしまわないようにするためのもの（Issue #99）。
//
// UNDERLINE / SUPERSCRIPT / SUBSCRIPT は装飾ビットであってスタイル番号ではない。
// 幅の計測側（GfxRenderer::getTextAdvanceX）も FONT_SELECT_MASK で落としてから
// 添字にするので、ここでも同じ扱いにして参照先を一致させる。
// 幅の計測にはスペースや CJK インデント参照文字（いずれも REGULAR）も使うため、
// REGULAR は常に含める。
uint8_t usedStyleMask(const std::vector<EpdFontFamily::Style>& wordStyles) {
  uint8_t mask = 1u << EpdFontFamily::REGULAR;
  for (const auto style : wordStyles) {
    const auto idx = static_cast<uint8_t>(style & EpdFontFamily::FONT_SELECT_MASK);
    mask |= static_cast<uint8_t>(1u << idx);
  }
  return mask;
}

// 段落（ブロック）の先頭が全角スペース（U+3000）で始まるか。
// 青空文庫などのテキストは行頭の全角スペースで字下げを表現しているので、
// その段落には自動の一行目インデントを重ねない（二重字下げの防止）。
bool startsWithIdeographicSpace(const std::vector<std::string>& words) {
  return !words.empty() && words.front().compare(0, 3, "\xe3\x80\x80") == 0;
}

// 空白だけでできた語か。<pre> の字下げのように、空白そのものを語として持つ場合、
// その前後にさらに語間を足すと空白が二重になる。
bool isSpaceOnlyWord(const std::string& word) {
  if (word.empty()) return false;
  for (const char c : word) {
    if (c != ' ') return false;
  }
  return true;
}

// Check if a word is a single CJK character (used for zero-spacing between adjacent CJK words)
bool isSingleCjkWord(const std::string& word) {
  if (word.empty()) return false;
  const auto* p = reinterpret_cast<const uint8_t*>(word.c_str());
  uint32_t cp;
  int len;
  if ((*p & 0x80) == 0) {
    cp = *p;
    len = 1;
  } else if ((*p & 0xE0) == 0xC0) {
    cp = *p & 0x1F;
    len = 2;
  } else if ((*p & 0xF0) == 0xE0) {
    cp = *p & 0x0F;
    len = 3;
  } else if ((*p & 0xF8) == 0xF0) {
    cp = *p & 0x07;
    len = 4;
  } else
    return false;
  for (int i = 1; i < len; i++) {
    if ((p[i] & 0xC0) != 0x80) return false;
    cp = (cp << 6) | (p[i] & 0x3F);
  }
  if (static_cast<int>(word.size()) != len) return false;
  return (cp >= 0x2E80 && cp <= 0x9FFF) || (cp >= 0x3000 && cp <= 0x30FF) || (cp >= 0x3400 && cp <= 0x4DBF) ||
         (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFF00 && cp <= 0xFFEF);
}

}  // namespace

void ParsedText::addWord(std::string word, const EpdFontFamily::Style fontStyle, const bool underline,
                         const bool attachToPrevious, const bool spaceBefore) {
  if (word.empty()) return;

  // Pre-allocate に合わせて容量を確保する。ChapterHtmlSlimParser の中間 flush 閾値
  // (MID_BLOCK_FLUSH_WORDS = 400) より十分大きく、かつ realloc が起きない値にする。
  // 過大に取ると初期割当時のヒープ圧迫（sizeof(std::string) * N ≈ 32*N bytes）と
  // フラグメンテーションのリスクが上がるため、512 程度が最適。
  // Without reserve(), std::vector doubles capacity on each reallocation (e.g. 512→1024),
  // requiring both old and new arrays in memory simultaneously. On a fragmented 380KB heap
  // this contiguous allocation can fail and call abort() (no C++ exceptions on ESP32).
  if (words.capacity() == 0) {
    words.reserve(512);
    wordStyles.reserve(512);
    wordContinues.reserve(512);
    wordSpaceBefore.reserve(512);
    // rubyTexts は遅延初期化（setRubyForWordAt時にのみ割り当て）— RAM節約
  }

  words.push_back(std::move(word));
  EpdFontFamily::Style combinedStyle = fontStyle;
  if (underline) {
    combinedStyle = static_cast<EpdFontFamily::Style>(combinedStyle | EpdFontFamily::UNDERLINE);
  }
  wordStyles.push_back(combinedStyle);
  wordContinues.push_back(attachToPrevious);
  wordSpaceBefore.push_back(spaceBefore);
}

void ParsedText::addWord(std::string word, const EpdFontFamily::Style fontStyle,
                         const VerticalTextUtils::VerticalBehavior vBehavior, const bool underline,
                         const bool attachToPrevious, const bool spaceBefore) {
  addWord(std::move(word), fontStyle, underline, attachToPrevious, spaceBefore);
  if (wordVerticalBehaviors.capacity() == 0) {
    wordVerticalBehaviors.reserve(512);
  }
  wordVerticalBehaviors.push_back(vBehavior);
}

void ParsedText::setRubyForWordAt(size_t index, const std::string& ruby) {
  if (index >= words.size()) return;
  // 遅延初期化: ルビが初めて設定された時にベクタを拡張
  if (rubyTexts.size() <= index) {
    rubyTexts.resize(words.size());
  }
  rubyTexts[index] = ruby;
}

// [start, end) の語に対応するルビを取り出す。
// rubyTexts は setRubyForWordAt() 時点の words.size() までしか伸びないため、
// その後 addWord() された語の分は短い。範囲全体を一括判定すると
// 末尾に語が追加された最終行（縦書きは最終列）だけルビが全て落ちるので、
// 存在する分だけ取り出し残りは空文字列で埋める。
// 取り出した範囲は呼び出し側が直後に erase するので、words と同様にムーブで済ませる
// （SSO を超えるルビ文字列のコピーによる malloc/free を避ける）。
std::vector<std::string> ParsedText::takeRubyRange(const size_t start, const size_t end) {
  std::vector<std::string> out(end - start);
  if (rubyTexts.size() > start) {
    const size_t avail = std::min(rubyTexts.size(), end) - start;
    std::move(rubyTexts.begin() + start, rubyTexts.begin() + start + avail, out.begin());
  }
  return out;
}

// Consumes data to minimize memory usage
void ParsedText::layoutAndExtractLines(const GfxRenderer& renderer, const int fontId, const uint16_t viewportWidth,
                                       const std::function<void(std::shared_ptr<TextBlock>)>& processLine,
                                       const bool includeLastLine) {
  if (words.empty()) {
    return;
  }
  SECTION_PERF_SCOPE(layoutUs);

  // Apply fixed transforms before any per-line layout work.
  applyParagraphIndent();

  // Ensure SD card font glyph metrics are loaded before measuring word widths.
  // For flash-based fonts isSdCardFont() returns false and this block is skipped
  // entirely — no heap allocation. For SD card fonts this reads glyph metadata
  // (advanceX only, no bitmaps) for all unique codepoints in this paragraph so
  // that calculateWordWidths() can measure text without on-demand SD I/O.
  if (renderer.isSdCardFont(fontId)) {
    SECTION_PERF_SCOPE(advanceUs);
    SECTION_PERF_COUNT(advanceCalls);
    std::string allText;
    for (size_t i = 0; i < words.size(); i++) {
      if (i > 0) allText += ' ';
      allText += words[i];
    }
    if (hyphenationEnabled) allText += '-';
    renderer.ensureSdCardFontReady(fontId, allText.c_str(), usedStyleMask(wordStyles));
  }

  // <sup> / <sub> は小さいフォントで測るので、そのぶんの advance も用意しておく。
  // 用意しないと lookupAdvance が NotCached を返して幅 0 になり、ページに焼き付いてしまう。
  if (TextBlock::smallFontId != 0 && TextBlock::smallFontId != fontId &&
      renderer.isSdCardFont(TextBlock::smallFontId)) {
    std::string scriptText;
    for (size_t i = 0; i < words.size(); i++) {
      if ((wordStyles[i] & EpdFontFamily::SCRIPT_MASK) != 0) scriptText += words[i];
    }
    if (!scriptText.empty()) {
      renderer.ensureSdCardFontReady(TextBlock::smallFontId, scriptText.c_str(), 1u << EpdFontFamily::REGULAR);
    }
  }

  const int pageWidth = viewportWidth;
  const int spaceWidth = renderer.getSpaceWidth(fontId, EpdFontFamily::REGULAR);

  // CJK fallback: when firstLineIndent is ON but CSS doesn't define text-indent,
  // calculate a 1-character CJK indent width and inject it as textIndent for layout.
  // Skip when textIndent is explicitly negative (hanging indent for <li> bullets),
  // and when the paragraph already starts with an ideographic space (explicit indent).
  if (firstLineIndent && blockStyle.textIndent == 0 && !blockStyle.textIndentDefined &&
      !startsWithIdeographicSpace(words) &&
      (blockStyle.alignment == CssTextAlign::Justify || blockStyle.alignment == CssTextAlign::Left)) {
    const int cjkCharWidth = renderer.getTextWidth(fontId, "\xe5\xad\x97", EpdFontFamily::REGULAR);
    blockStyle.textIndent = static_cast<int16_t>(cjkCharWidth > 0 ? cjkCharWidth : spaceWidth * 3);
  }

  auto wordWidths = calculateWordWidths(renderer, fontId);

  // リスト項目のぶら下げ幅を、マーカー語の実測幅に合わせ直す（保険）。
  // パーサは <li> を見た時点でこの幅を見積もる。その時点で SD カードフォントの advance を
  // 用意させてあるので普通は一致するが、フォントが字を持たない等でずれても
  // 折り返し位置が外れないようにしておく。
  // 動かすのは textIndent だけ。paddingLeft はブロックの左端であり、呼び出し元が
  // それを使って折り返し幅（effectiveWidth）を先に確定させているので、ここで動かすと
  // 行が右へはみ出す。textIndent だけならマーカーが実測幅ぶん左へ出るだけで、
  // 折り返した行の頭は本文 1 文字目に揃う。
  if (!hangIndentAligned && blockStyle.isListItem && blockStyle.textIndentDefined && blockStyle.textIndent < 0 &&
      !wordWidths.empty()) {
    hangIndentAligned = true;
    blockStyle.textIndent = static_cast<int16_t>(-static_cast<int16_t>(wordWidths[0]));
  }

  // Build indexed continues vector from the parallel list for O(1) access during layout
  std::vector<bool> continuesVec(wordContinues.begin(), wordContinues.end());

  // 語間を詰める境界のフラグ。tightVec[i] は「words[i] と words[i-1] の間に
  // 空白を入れない」を表す。CJK は 1 文字ずつを語に割っているので、そのままでは
  // 全ての文字の間に語間が入ってしまう。条件は 2 つ:
  //   - 原文でその境界に空白が無かった（あれば普通の語間として残す。
  //     「第一章 序」のように CJK の間に半角スペースを置いた原文で空白が消えない）
  //   - どちらかが CJK 1 文字（「あい」も「5m<sup>2</sup>、」もブラウザは詰めて出す）か、
  //     どちらかが空白だけの語（<pre> の字下げ。語間を足すと空白が二重になる）
  std::vector<bool> cjkAdjVec;
  cjkAdjVec.reserve(words.size());
  bool prevTight = false;
  for (size_t i = 0; i < words.size(); ++i) {
    const bool tight = isSingleCjkWord(words[i]) || isSpaceOnlyWord(words[i]);
    const bool hasSpace = i < wordSpaceBefore.size() && wordSpaceBefore[i];
    cjkAdjVec.push_back(i > 0 && !hasSpace && (tight || prevTight));
    prevTight = tight;
  }

  std::vector<size_t> lineBreakIndices;
  if (hyphenationEnabled) {
    lineBreakIndices =
        computeHyphenatedLineBreaks(renderer, fontId, pageWidth, spaceWidth, wordWidths, continuesVec, cjkAdjVec);
  } else {
    lineBreakIndices = computeLineBreaks(renderer, fontId, pageWidth, spaceWidth, wordWidths, continuesVec, cjkAdjVec);
  }
  const size_t lineCount = includeLastLine ? lineBreakIndices.size() : lineBreakIndices.size() - 1;

  for (size_t i = 0; i < lineCount; ++i) {
    extractLine(i, pageWidth, spaceWidth, wordWidths, continuesVec, cjkAdjVec, lineBreakIndices, processLine, renderer,
                fontId);
  }

  // Remove consumed words so size() reflects only remaining words
  if (lineCount > 0) {
    const size_t consumed = lineBreakIndices[lineCount - 1];
    words.erase(words.begin(), words.begin() + consumed);
    wordStyles.erase(wordStyles.begin(), wordStyles.begin() + consumed);
    wordContinues.erase(wordContinues.begin(), wordContinues.begin() + consumed);
    if (!wordSpaceBefore.empty()) {
      wordSpaceBefore.erase(wordSpaceBefore.begin(),
                            wordSpaceBefore.begin() + std::min(consumed, wordSpaceBefore.size()));
    }
    if (!wordVerticalBehaviors.empty()) {
      const size_t vbConsumed = std::min(consumed, wordVerticalBehaviors.size());
      wordVerticalBehaviors.erase(wordVerticalBehaviors.begin(), wordVerticalBehaviors.begin() + vbConsumed);
    }
    if (!rubyTexts.empty()) {
      const size_t rtConsumed = std::min(consumed, rubyTexts.size());
      rubyTexts.erase(rubyTexts.begin(), rubyTexts.begin() + rtConsumed);
    }
  }
}

void ParsedText::layoutVerticalColumns(const GfxRenderer& renderer, const int fontId, const uint16_t columnHeight,
                                       const std::function<void(std::shared_ptr<TextBlock>)>& processColumn,
                                       const bool includeLastColumn) {
  if (words.empty()) return;
  SECTION_PERF_SCOPE(layoutUs);

  // Ensure SD card font metrics are loaded
  if (renderer.isSdCardFont(fontId)) {
    SECTION_PERF_SCOPE(advanceUs);
    SECTION_PERF_COUNT(advanceCalls);
    std::string allText;
    for (const auto& w : words) {
      if (InlineImage::isInlineImage(w)) continue;  // 画像語に対応するグリフは無い
      allText += w;
      allText += ' ';
    }
    renderer.ensureSdCardFontReady(fontId, allText.c_str(), usedStyleMask(wordStyles));
  }

  const int lineHeight = renderer.getLineHeight(fontId);

  // Compute CJK character advance once from the first Upright word.
  // This is used as the reference cell height for TateChuYoko and spacing.
  // Cannot use a hardcoded reference char ("一") because it may not be in the advance table.
  int cjkCharAdvance = 0;
  for (size_t i = 0; i < words.size() && cjkCharAdvance == 0; i++) {
    if (InlineImage::isInlineImage(words[i])) continue;  // 画像は文字送りの基準にしない
    auto vb =
        (i < wordVerticalBehaviors.size()) ? wordVerticalBehaviors[i] : VerticalTextUtils::VerticalBehavior::Upright;
    if (vb == VerticalTextUtils::VerticalBehavior::Upright) {
      cjkCharAdvance = renderer.getTextAdvanceX(fontId, words[i].c_str(), wordStyles[i]);
    }
  }
  if (cjkCharAdvance == 0) cjkCharAdvance = lineHeight;  // fallback

  // Calculate word heights for vertical layout
  std::vector<uint16_t> wordHeights;
  wordHeights.reserve(words.size());
  const int sp = renderer.getVerticalCharSpacing();
  const int cjkSpacing = cjkCharAdvance * sp / 100;

  for (size_t i = 0; i < words.size(); i++) {
    // インライン画像は文字ではないので、送り量は画像の高さそのもの（＋通常の字間）
    {
      std::string imagePath;
      int imageWidth = 0;
      int imageHeight = 0;
      if (InlineImage::decode(words[i], imagePath, imageWidth, imageHeight)) {
        wordHeights.push_back(static_cast<uint16_t>(imageHeight + cjkSpacing));
        continue;
      }
    }
    auto vb =
        (i < wordVerticalBehaviors.size()) ? wordVerticalBehaviors[i] : VerticalTextUtils::VerticalBehavior::Upright;
    uint16_t baseHeight;
    switch (vb) {
      case VerticalTextUtils::VerticalBehavior::Sideways:
        baseHeight = renderer.getTextAdvanceX(fontId, words[i].c_str(), wordStyles[i]);
        break;
      case VerticalTextUtils::VerticalBehavior::TateChuYoko:
        baseHeight = static_cast<uint16_t>(cjkCharAdvance);
        break;
      default:
        baseHeight = renderer.getTextAdvanceX(fontId, words[i].c_str(), wordStyles[i]);
        break;
    }
    if (vb == VerticalTextUtils::VerticalBehavior::Upright) {
      wordHeights.push_back(baseHeight + baseHeight * sp / 100);
    } else {
      wordHeights.push_back(baseHeight + cjkSpacing);
    }
  }

  // Compute first-line indent for vertical mode (same conditions as horizontal).
  int verticalIndent = 0;
  if (firstLineIndent && blockStyle.textIndent == 0 && !blockStyle.textIndentDefined &&
      !startsWithIdeographicSpace(words) &&
      (blockStyle.alignment == CssTextAlign::Justify || blockStyle.alignment == CssTextAlign::Left)) {
    verticalIndent = cjkCharAdvance > 0 ? cjkCharAdvance : lineHeight;
  }

  // 禁則処理で参照する継続フラグ。wordContinues と同じ内容だが、
  // adjustBreakForKinsoku() が std::vector<bool> を取るのでここで用意する。
  const std::vector<bool> continuesVec(wordContinues.begin(), wordContinues.end());

  // First pass: compute column boundaries without emitting.
  // columnEnds[i] is the exclusive end index of column i (= start of column i+1).
  std::vector<size_t> columnEnds;
  {
    size_t columnStart = 0;
    int currentY = verticalIndent;
    for (size_t i = 0; i < words.size(); i++) {
      if (currentY + wordHeights[i] > columnHeight && i > columnStart) {
        // 禁則処理（追い出し）。行頭禁則・行末禁則・分離禁止を収束するまで交互に見る。
        const size_t breakAt = Kinsoku::adjustBreak(words, continuesVec, i, columnStart);
        columnEnds.push_back(breakAt);
        columnStart = breakAt;
        currentY = 0;
        for (size_t j = columnStart; j <= i; j++) {
          currentY += wordHeights[j];
        }
        continue;
      }
      currentY += wordHeights[i];
    }
    if (columnStart < words.size()) {
      columnEnds.push_back(words.size());
    }
  }

  // Determine how many columns to emit. Mid-block flushes pass includeLastColumn=false
  // so the trailing partial column is preserved for the next layout call (avoids visually
  // short columns at flush boundaries). makePages calls use the default true to flush all.
  const size_t totalCols = columnEnds.size();
  const size_t emitCols = (includeLastColumn || totalCols <= 1) ? totalCols : totalCols - 1;

  // Second pass: emit columns up to emitCols.
  bool isFirstColumn = true;
  size_t emitStart = 0;
  for (size_t i = 0; i < emitCols; i++) {
    const size_t start = emitStart;
    const size_t end = columnEnds[i];
    std::vector<std::string> colWords(std::make_move_iterator(words.begin() + start),
                                      std::make_move_iterator(words.begin() + end));
    std::vector<int16_t> colYpos;
    std::vector<int16_t> colXpos;
    std::vector<EpdFontFamily::Style> colStyles(wordStyles.begin() + start, wordStyles.begin() + end);
    const size_t count = end - start;
    std::vector<std::string> colRubyTexts = takeRubyRange(start, end);
    colYpos.reserve(count);
    colXpos.resize(count, 0);

    int y = isFirstColumn ? verticalIndent : 0;
    for (size_t j = start; j < end; j++) {
      colYpos.push_back(static_cast<int16_t>(y));
      y += wordHeights[j];
    }

    processColumn(std::make_shared<TextBlock>(std::move(colWords), std::move(colXpos), std::move(colStyles), blockStyle,
                                              std::move(colYpos), true, std::move(colRubyTexts)));
    isFirstColumn = false;
    emitStart = end;
  }

  // Erase only consumed words. Words from emitStart onwards remain in the TextBlock
  // for the next layout call to render together with newly accumulated text.
  if (emitStart > 0) {
    words.erase(words.begin(), words.begin() + emitStart);
    wordStyles.erase(wordStyles.begin(), wordStyles.begin() + emitStart);
    wordContinues.erase(wordContinues.begin(), wordContinues.begin() + emitStart);
    if (!wordSpaceBefore.empty()) {
      wordSpaceBefore.erase(wordSpaceBefore.begin(),
                            wordSpaceBefore.begin() + std::min(emitStart, wordSpaceBefore.size()));
    }
    if (!wordVerticalBehaviors.empty()) {
      const size_t vbConsumed = std::min(emitStart, wordVerticalBehaviors.size());
      wordVerticalBehaviors.erase(wordVerticalBehaviors.begin(), wordVerticalBehaviors.begin() + vbConsumed);
    }
    if (!rubyTexts.empty()) {
      const size_t rtConsumed = std::min(emitStart, rubyTexts.size());
      rubyTexts.erase(rubyTexts.begin(), rubyTexts.begin() + rtConsumed);
    }
  }
}

std::vector<uint16_t> ParsedText::calculateWordWidths(const GfxRenderer& renderer, const int fontId) {
  std::vector<uint16_t> wordWidths;
  wordWidths.reserve(words.size());

  for (size_t i = 0; i < words.size(); ++i) {
    wordWidths.push_back(measureWordWidth(renderer, fontId, words[i], wordStyles[i]));
  }

  return wordWidths;
}

std::vector<size_t> ParsedText::computeLineBreaks(const GfxRenderer& renderer, const int fontId, const int pageWidth,
                                                  const int spaceWidth, std::vector<uint16_t>& wordWidths,
                                                  std::vector<bool>& continuesVec, std::vector<bool>& cjkAdjVec) {
  if (words.empty()) {
    return {};
  }

  // Compute first-line indent:
  // - Hanging indent (negative textIndent + textIndentDefined): always applied (e.g. <li> bullet)
  // - Positive first-line indent: requires user toggle (firstLineIndent)
  const bool isHangingIndent = blockStyle.textIndentDefined && blockStyle.textIndent < 0;
  const bool isFirstLineIndent = firstLineIndent && blockStyle.textIndent > 0;
  const int effectiveIndent =
      (isHangingIndent || isFirstLineIndent) &&
              (blockStyle.alignment == CssTextAlign::Justify || blockStyle.alignment == CssTextAlign::Left)
          ? blockStyle.textIndent
          : 0;

  // Ensure any word that would overflow even as the first entry on a line is split using fallback hyphenation.
  for (size_t i = 0; i < wordWidths.size(); ++i) {
    const int effectiveWidth = i == 0 ? pageWidth - effectiveIndent : pageWidth;
    while (wordWidths[i] > effectiveWidth) {
      if (!hyphenateWordAtIndex(i, effectiveWidth, renderer, fontId, wordWidths, /*allowFallbackBreaks=*/true,
                                &continuesVec, &cjkAdjVec)) {
        break;
      }
    }
  }

  // Greedy forward scan
  std::vector<size_t> lineBreakIndices;
  size_t currentIndex = 0;
  bool isFirstLine = true;

  while (currentIndex < wordWidths.size()) {
    const size_t lineStart = currentIndex;
    const int effectivePageWidth = isFirstLine ? pageWidth - effectiveIndent : pageWidth;
    int lineWidth = 0;

    while (currentIndex < wordWidths.size()) {
      const bool isFirstWord = currentIndex == lineStart;
      const bool cjkAdj = !isFirstWord && currentIndex < cjkAdjVec.size() && cjkAdjVec[currentIndex];
      const int gap = isFirstWord || continuesVec[currentIndex] || cjkAdj ? 0 : spaceWidth;
      const int candidateWidth = gap + wordWidths[currentIndex];

      if (lineWidth + candidateWidth <= effectivePageWidth) {
        lineWidth += candidateWidth;
        ++currentIndex;
        continue;
      }

      if (currentIndex == lineStart) {
        ++currentIndex;
      }
      break;
    }

    // 禁則処理（追い出し）。収束しなかった場合は元の位置が返るので、
    // 継続語（スペース無しで前の語に続く語）の保護は下のループで別途かける。
    currentIndex = Kinsoku::adjustBreak(words, continuesVec, currentIndex, lineStart);

    while (currentIndex > lineStart + 1 && currentIndex < wordWidths.size() && continuesVec[currentIndex]) {
      --currentIndex;
    }

    lineBreakIndices.push_back(currentIndex);
    isFirstLine = false;
  }

  return lineBreakIndices;
}

void ParsedText::applyParagraphIndent() {
  if (words.empty()) {
    return;
  }

  if (firstLineIndent || blockStyle.textIndentDefined || startsWithIdeographicSpace(words)) {
    // Indent is applied as pixel offset during layout (firstLineIndent toggle or CSS text-indent),
    // or the paragraph already carries an explicit ideographic-space indent.
  } else if (blockStyle.alignment == CssTextAlign::Justify || blockStyle.alignment == CssTextAlign::Left) {
    // No indent configured - use EmSpace fallback for visual indent
    words.front().insert(0, "\xe2\x80\x83");
  }
}

// Builds break indices while opportunistically splitting the word that would overflow the current line.
std::vector<size_t> ParsedText::computeHyphenatedLineBreaks(const GfxRenderer& renderer, const int fontId,
                                                            const int pageWidth, const int spaceWidth,
                                                            std::vector<uint16_t>& wordWidths,
                                                            std::vector<bool>& continuesVec,
                                                            std::vector<bool>& cjkAdjVec) {
  const bool isHangingIndent2 = blockStyle.textIndentDefined && blockStyle.textIndent < 0;
  const bool isFirstLineIndent2 = firstLineIndent && blockStyle.textIndent > 0;
  const int effectiveIndent =
      (isHangingIndent2 || isFirstLineIndent2) &&
              (blockStyle.alignment == CssTextAlign::Justify || blockStyle.alignment == CssTextAlign::Left)
          ? blockStyle.textIndent
          : 0;

  std::vector<size_t> lineBreakIndices;
  size_t currentIndex = 0;
  bool isFirstLine = true;

  while (currentIndex < wordWidths.size()) {
    const size_t lineStart = currentIndex;
    const int effectivePageWidth = isFirstLine ? pageWidth - effectiveIndent : pageWidth;
    int lineWidth = 0;
    bool hyphenatedAtBreak = false;

    while (currentIndex < wordWidths.size()) {
      const bool isFirstWord = currentIndex == lineStart;
      const bool cjkAdj = !isFirstWord && currentIndex < cjkAdjVec.size() && cjkAdjVec[currentIndex];
      const int spacing = isFirstWord || continuesVec[currentIndex] || cjkAdj ? 0 : spaceWidth;
      const int candidateWidth = spacing + wordWidths[currentIndex];

      if (lineWidth + candidateWidth <= effectivePageWidth) {
        lineWidth += candidateWidth;
        ++currentIndex;
        continue;
      }

      const int availableWidth = effectivePageWidth - lineWidth - spacing;
      const bool allowFallbackBreaks = isFirstWord;

      if (availableWidth > 0 && hyphenateWordAtIndex(currentIndex, availableWidth, renderer, fontId, wordWidths,
                                                     allowFallbackBreaks, &continuesVec, &cjkAdjVec)) {
        lineWidth += spacing + wordWidths[currentIndex];
        ++currentIndex;
        hyphenatedAtBreak = true;
        break;
      }

      if (currentIndex == lineStart) {
        lineWidth += candidateWidth;
        ++currentIndex;
      }
      break;
    }

    // 禁則処理（追い出し）。直前でハイフン分割した行では行わない。追い出すと
    // 挿入済みのハイフンごと次行へ送られ、行の途中に "exam-ple" が現れてしまう。
    if (!hyphenatedAtBreak) {
      currentIndex = Kinsoku::adjustBreak(words, continuesVec, currentIndex, lineStart);
    }

    while (currentIndex > lineStart + 1 && currentIndex < wordWidths.size() && continuesVec[currentIndex]) {
      --currentIndex;
    }

    lineBreakIndices.push_back(currentIndex);
    isFirstLine = false;
  }

  return lineBreakIndices;
}

bool ParsedText::hyphenateWordAtIndex(const size_t wordIndex, const int availableWidth, const GfxRenderer& renderer,
                                      const int fontId, std::vector<uint16_t>& wordWidths,
                                      const bool allowFallbackBreaks, std::vector<bool>* continuesVec,
                                      std::vector<bool>* cjkAdjVec) {
  if (availableWidth <= 0 || wordIndex >= words.size()) {
    return false;
  }

  const std::string& word = words[wordIndex];
  const auto style = wordStyles[wordIndex];

  auto breakInfos = Hyphenator::breakOffsets(word, allowFallbackBreaks);
  if (breakInfos.empty()) {
    return false;
  }

  size_t chosenOffset = 0;
  int chosenWidth = -1;
  bool chosenNeedsHyphen = true;

  for (const auto& info : breakInfos) {
    const size_t offset = info.byteOffset;
    if (offset == 0 || offset >= word.size()) {
      continue;
    }

    const bool needsHyphen = info.requiresInsertedHyphen;
    const int prefixWidth = measureWordWidth(renderer, fontId, word.substr(0, offset), style, needsHyphen);
    if (prefixWidth > availableWidth || prefixWidth <= chosenWidth) {
      continue;
    }

    chosenWidth = prefixWidth;
    chosenOffset = offset;
    chosenNeedsHyphen = needsHyphen;
  }

  if (chosenWidth < 0) {
    return false;
  }

  std::string remainder = word.substr(chosenOffset);
  words[wordIndex].resize(chosenOffset);
  if (chosenNeedsHyphen) {
    words[wordIndex].push_back('-');
  }

  words.insert(words.begin() + wordIndex + 1, remainder);
  wordStyles.insert(wordStyles.begin() + wordIndex + 1, style);
  wordContinues.insert(wordContinues.begin() + wordIndex + 1, false);
  if (wordIndex + 1 <= wordSpaceBefore.size()) {
    wordSpaceBefore.insert(wordSpaceBefore.begin() + wordIndex + 1, false);
  }
  if (wordIndex + 1 <= rubyTexts.size()) {
    rubyTexts.insert(rubyTexts.begin() + wordIndex + 1, "");
  }

  if (continuesVec) {
    continuesVec->insert(continuesVec->begin() + wordIndex + 1, false);
  }

  if (cjkAdjVec) {
    // 分割された前半と後半の間は同じ語の続きなので空白は入らない。
    // 分割後の語は Latin（ハイフネーション対象）なので CJK 隣接にはならないが、
    // フォールバック分割で CJK を割ることもあるため実際に判定しておく。
    const auto isTight = [](const std::string& w) { return isSingleCjkWord(w) || isSpaceOnlyWord(w); };
    const bool prefixTight = isTight(words[wordIndex]);
    const bool remainderTight = isTight(remainder);
    if (wordIndex > 0) {
      (*cjkAdjVec)[wordIndex] = (prefixTight || isTight(words[wordIndex - 1])) &&
                                (wordIndex >= wordSpaceBefore.size() || !wordSpaceBefore[wordIndex]);
    }
    cjkAdjVec->insert(cjkAdjVec->begin() + wordIndex + 1, remainderTight || prefixTight);
  }

  wordWidths[wordIndex] = static_cast<uint16_t>(chosenWidth);
  const uint16_t remainderWidth = measureWordWidth(renderer, fontId, remainder, style);
  wordWidths.insert(wordWidths.begin() + wordIndex + 1, remainderWidth);
  return true;
}

void ParsedText::extractLine(const size_t breakIndex, const int pageWidth, const int spaceWidth,
                             const std::vector<uint16_t>& wordWidths, const std::vector<bool>& continuesVec,
                             const std::vector<bool>& cjkAdjVec, const std::vector<size_t>& lineBreakIndices,
                             const std::function<void(std::shared_ptr<TextBlock>)>& processLine,
                             const GfxRenderer& renderer, const int fontId) {
  const size_t lineBreak = lineBreakIndices[breakIndex];
  const size_t lastBreakAt = breakIndex > 0 ? lineBreakIndices[breakIndex - 1] : 0;
  const size_t lineWordCount = lineBreak - lastBreakAt;

  const bool isFirstLine = breakIndex == 0;
  const bool isHangingIndent3 = blockStyle.textIndentDefined && blockStyle.textIndent < 0;
  const bool isFirstLineIndent3 = firstLineIndent && blockStyle.textIndent > 0;
  const int effectiveIndent =
      isFirstLine && (isHangingIndent3 || isFirstLineIndent3) &&
              (blockStyle.alignment == CssTextAlign::Justify || blockStyle.alignment == CssTextAlign::Left)
          ? blockStyle.textIndent
          : 0;

  int lineWordWidthSum = 0;
  size_t actualGapCount = 0;
  size_t nonCjkGapCount = 0;

  for (size_t wordIdx = 0; wordIdx < lineWordCount; wordIdx++) {
    lineWordWidthSum += wordWidths[lastBreakAt + wordIdx];
    if (wordIdx > 0 && !continuesVec[lastBreakAt + wordIdx]) {
      actualGapCount++;
      const bool cjkAdj = cjkAdjVec[lastBreakAt + wordIdx];
      if (!cjkAdj) {
        nonCjkGapCount++;
      }
    }
  }

  const int effectivePageWidth = pageWidth - effectiveIndent;
  // 語間として必ず入る空白のぶん。均等割りではこれを取り置いてから残りを配る。
  // 取り置かずに全余白を配ると、半角スペース 1 つぶんの差が行全体にならされて消え、
  // 「第一章 序」のような CJK どうしの区切りが見えなくなる。
  const int naturalGapWidth = static_cast<int>(nonCjkGapCount) * spaceWidth;
  const int freeSpace = effectivePageWidth - lineWordWidthSum - naturalGapWidth;

  const bool isLastLine = breakIndex == lineBreakIndices.size() - 1;
  const bool isJustified = blockStyle.alignment == CssTextAlign::Justify && !isLastLine && actualGapCount >= 1;

  int justifiedSpacing = 0;
  if (isJustified && freeSpace > 0) {
    justifiedSpacing = freeSpace / static_cast<int>(actualGapCount);
  }

  auto xpos = static_cast<int16_t>(effectiveIndent);
  if (blockStyle.alignment == CssTextAlign::Right) {
    xpos = freeSpace;
  } else if (blockStyle.alignment == CssTextAlign::Center) {
    xpos = freeSpace / 2;
  }

  std::vector<int16_t> lineXPos;
  lineXPos.reserve(lineWordCount);

  for (size_t wordIdx = 0; wordIdx < lineWordCount; wordIdx++) {
    const uint16_t currentWordWidth = wordWidths[lastBreakAt + wordIdx];

    lineXPos.push_back(xpos);

    const bool nextIsContinuation = wordIdx + 1 < lineWordCount && continuesVec[lastBreakAt + wordIdx + 1];
    int gap = 0;
    if (!nextIsContinuation && wordIdx + 1 < lineWordCount) {
      // 自然な語間（詰める境界なら 0、そうでなければ空白 1 つ）に、
      // 均等割りの取り分を足す。置き換えではなく加算なので空白が残る。
      const bool nextCjkAdj = cjkAdjVec[lastBreakAt + wordIdx + 1];
      gap = nextCjkAdj ? 0 : spaceWidth;
      if (isJustified) {
        gap += justifiedSpacing;
      }
    }

    xpos += currentWordWidth + gap;
  }

  std::vector<std::string> lineWords(std::make_move_iterator(words.begin() + lastBreakAt),
                                     std::make_move_iterator(words.begin() + lineBreak));
  std::vector<EpdFontFamily::Style> lineWordStyles(wordStyles.begin() + lastBreakAt, wordStyles.begin() + lineBreak);
  std::vector<std::string> lineRubyTexts = takeRubyRange(lastBreakAt, lineBreak);

  for (auto& word : lineWords) {
    if (containsSoftHyphen(word)) {
      stripSoftHyphensInPlace(word);
    }
  }

  // コードブロックの枠。左右の縦線は全ての行に引くが、上辺はブロックの最初の行だけ、
  // 下辺は最後の行だけ。1 つの <pre> 行が折り返した場合も含めて視覚上の行で数える。
  // includeLastLine=false（中間 flush）のときは isLastLine が立たないので、
  // 下辺は最終回の呼び出しまで付かない。
  BlockStyle lineStyle = blockStyle;
  if (lineStyle.frameEdges != 0) {
    if (frameTopEmitted) {
      lineStyle.frameEdges = static_cast<uint8_t>(lineStyle.frameEdges & ~BlockStyle::FRAME_TOP);
    }
    if (!isLastLine) {
      lineStyle.frameEdges = static_cast<uint8_t>(lineStyle.frameEdges & ~BlockStyle::FRAME_BOTTOM);
    }
    frameTopEmitted = true;
  }

  processLine(std::make_shared<TextBlock>(std::move(lineWords), std::move(lineXPos), std::move(lineWordStyles),
                                          lineStyle, std::vector<int16_t>{}, false, std::move(lineRubyTexts)));
}
