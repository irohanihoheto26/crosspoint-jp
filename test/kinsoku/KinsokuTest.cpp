// 禁則処理（lib/Epub/Epub/Kinsoku.h）のホスト側テスト。
//
//   ./test/run_kinsoku_test.sh
//
// 実機やシミュレータを使わずに、行分割位置の追い出しが規則どおりかを確かめる。
// 前半は文字分類の単体テスト、後半は「CJK 1 文字 = 1 語」というパーサの語分割を
// 再現したうえで貪欲法の行分割をかけ、どの行幅でも禁則違反が残らないことを見る。

#include "Epub/Kinsoku.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(const bool ok, const std::string& what) {
  if (!ok) {
    std::printf("  FAIL: %s\n", what.c_str());
    failures++;
  }
}

// UTF-8 文字列を「1 コードポイント = 1 語」に割る。CJK 本文に対しては
// ChapterHtmlSlimParser の語分割と同じ結果になる。
std::vector<std::string> splitCodepoints(const std::string& text) {
  std::vector<std::string> words;
  size_t i = 0;
  while (i < text.size()) {
    const auto b0 = static_cast<unsigned char>(text[i]);
    size_t len = 1;
    if ((b0 & 0xE0) == 0xC0) {
      len = 2;
    } else if ((b0 & 0xF0) == 0xE0) {
      len = 3;
    } else if ((b0 & 0xF8) == 0xF0) {
      len = 4;
    }
    if (i + len > text.size()) len = 1;
    words.push_back(text.substr(i, len));
    i += len;
  }
  return words;
}

// ParsedText::computeLineBreaks の貪欲法を、1 語 1 単位幅として再現する。
// 返すのは各行の終端（exclusive）。
std::vector<size_t> greedyBreaks(const std::vector<std::string>& words, const size_t lineLen, const bool kinsoku) {
  const std::vector<bool> continues(words.size(), false);
  std::vector<size_t> breaks;
  size_t i = 0;
  while (i < words.size()) {
    const size_t lineStart = i;
    i = std::min(words.size(), lineStart + lineLen);
    if (kinsoku) i = Kinsoku::adjustBreak(words, continues, i, lineStart);
    if (i <= lineStart) i = lineStart + 1;  // 前進しない組み合わせは作らない（保険）
    breaks.push_back(i);
  }
  return breaks;
}

// 行頭・行末・分離禁止の違反数を数える。最終行の後ろには行が無いので数えない。
int countViolations(const std::vector<std::string>& words, const std::vector<size_t>& breaks) {
  int n = 0;
  for (size_t k = 0; k + 1 < breaks.size(); k++) {
    const size_t b = breaks[k];
    const uint32_t head = Kinsoku::firstCodepoint(words[b]);
    const uint32_t tail = Kinsoku::lastCodepoint(words[b - 1]);
    if (Kinsoku::isLineStartProhibited(head)) n++;
    if (Kinsoku::isLineEndProhibited(tail)) n++;
    if (Kinsoku::isInseparablePair(tail, head)) n++;
  }
  return n;
}

std::string repeat(const std::string& s, const int times) {
  std::string out;
  for (int i = 0; i < times; i++) out += s;
  return out;
}

// --- 文字分類 --------------------------------------------------------------

void testClassification() {
  std::printf("[classification]\n");

  // 行頭禁則
  const struct {
    uint32_t cp;
    const char* name;
  } startProhibited[] = {
      {0x3001, "、"},  {0x3002, "。"},  {0xFF0C, "，"},  {0xFF0E, "．"},  {0x30FB, "・"},  {0xFF1A, "："},
      {0xFF1B, "；"},  {0xFF01, "！"},  {0xFF1F, "？"},  {0x300D, "」"},  {0x300F, "』"},  {0x3011, "】"},
      {0xFF09, "）"},  {0xFF3D, "］"},  {0xFF5D, "｝"},  {0x300B, "》"},  {0x3009, "〉"},  {0x201D, "”"},
      {0x2019, "’"},   {0x301F, "〟"},  {0x30FC, "ー"},  {0xFF70, "ｰ"},   {0x3005, "々"},  {0x309D, "ゝ"},
      {0x309E, "ゞ"},  {0x30FD, "ヽ"},  {0x30FE, "ヾ"},  {0x303B, "〻"},  {0x301C, "〜"},  {0xFF5E, "～"},
      {0x2010, "‐"},   {0x30A0, "゠"},  {0x3063, "っ"},  {0x3083, "ゃ"},  {0x30C3, "ッ"},  {0x30F6, "ヶ"},
      {0x31F0, "ㇰ"},  {0xFF9E, "ﾞ"},   {0xFF05, "％"},  {0x2103, "℃"},
  };
  for (const auto& e : startProhibited) {
    check(Kinsoku::isLineStartProhibited(e.cp), std::string("行頭禁則のはず: ") + e.name);
  }

  // 行末禁則
  const struct {
    uint32_t cp;
    const char* name;
  } endProhibited[] = {
      {0x300C, "「"}, {0x300E, "『"}, {0x3010, "【"}, {0x3014, "〔"}, {0xFF08, "（"}, {0xFF3B, "［"},
      {0xFF5B, "｛"}, {0x300A, "《"}, {0x3008, "〈"}, {0x201C, "“"},  {0x2018, "‘"},  {0x301D, "〝"},
      {0x0024, "$"},  {0x00A5, "¥"},  {0x2116, "№"}, {0x3012, "〒"},
  };
  for (const auto& e : endProhibited) {
    check(Kinsoku::isLineEndProhibited(e.cp), std::string("行末禁則のはず: ") + e.name);
  }

  // 通常の文字はどちらでもない
  const struct {
    uint32_t cp;
    const char* name;
  } ordinary[] = {
      {0x3042, "あ"}, {0x4E00, "一"}, {0x30A2, "ア"}, {0x0041, "A"},
      {0x3000, "全角空白"}, {0x2026, "…"}, {0x2015, "―"},
  };
  for (const auto& e : ordinary) {
    check(!Kinsoku::isLineStartProhibited(e.cp), std::string("行頭禁則ではないはず: ") + e.name);
    check(!Kinsoku::isLineEndProhibited(e.cp), std::string("行末禁則ではないはず: ") + e.name);
  }

  // 分離禁止は「同じ文字が連続したとき」だけ
  check(Kinsoku::isInseparablePair(0x2026, 0x2026), "…… は分離禁止");
  check(Kinsoku::isInseparablePair(0x2015, 0x2015), "―― は分離禁止");
  check(!Kinsoku::isInseparablePair(0x2026, 0x3042), "…あ は分離禁止ではない");
  check(!Kinsoku::isInseparablePair(0x3042, 0x3042), "ああ は分離禁止ではない");
}

// --- 行分割 ----------------------------------------------------------------

void testLineBreaking() {
  std::printf("[line breaking]\n");

  const struct {
    const char* label;
    std::string text;
  } samples[] = {
      {"句読点", repeat("吾輩は猫である、名前はまだ無い。", 12)},
      {"閉じ括弧・開き括弧", repeat("「そうですか」と彼は言った。（本当に？）と私は思った。", 8)},
      {"小書き仮名・長音・中点", repeat("しゃっくりが止まった、コーヒー・紅茶・ジュース。", 8)},
      {"繰返し記号・ハイフン類", repeat("日々是好日、いろゝゝの事、東京〜大阪の移動。", 8)},
      {"分離禁止", repeat("そして……彼は――去った。残されたのは沈黙……だけ。", 8)},
      {"感嘆符・疑問符", repeat("なんだって！本当か？そんな馬鹿な！ありえない？", 8)},
  };

  for (const auto& s : samples) {
    const auto words = splitCodepoints(s.text);
    int before = 0;
    int after = 0;
    // 行幅を変えると約物の落ちる位置が変わる。実機の 1 行は 15〜40 文字程度。
    for (size_t lineLen = 8; lineLen <= 45; lineLen++) {
      before += countViolations(words, greedyBreaks(words, lineLen, false));
      after += countViolations(words, greedyBreaks(words, lineLen, true));
    }
    std::printf("  %-24s 違反 %3d → %d\n", s.label, before, after);
    check(before > 0, std::string("テストとして成立していない（修正前の違反が 0）: ") + s.label);
    check(after == 0, std::string("禁則違反が残った: ") + s.label);
  }
}

// --- 追い出しが収束しない場合 ----------------------------------------------

void testPathological() {
  std::printf("[pathological]\n");

  // 行が約物だけで埋まる場合、追い出しても違反を消せない。行が空にならず、
  // レイアウトが必ず前進することだけを保証する。
  const auto words = splitCodepoints(repeat("。", 40));
  const std::vector<bool> continues(words.size(), false);
  for (size_t lineLen = 2; lineLen <= 10; lineLen++) {
    size_t i = 0;
    int iterations = 0;
    while (i < words.size()) {
      const size_t lineStart = i;
      const size_t candidate = std::min(words.size(), lineStart + lineLen);
      i = Kinsoku::adjustBreak(words, continues, candidate, lineStart);
      check(i > lineStart, "追い出しで行が空になった");
      if (i <= lineStart) break;
      check(++iterations <= static_cast<int>(words.size()), "行分割が前進していない");
    }
  }

  // 段落末（breakAt == words.size()）では追い出さない
  const auto tail = splitCodepoints("あい「");
  check(Kinsoku::adjustBreak(tail, std::vector<bool>(tail.size(), false), tail.size(), 0) == tail.size(),
        "段落末で追い出してしまった");
}

}  // namespace

int main() {
  testClassification();
  testLineBreaking();
  testPathological();

  if (failures == 0) {
    std::printf("\nすべて成功\n");
    return 0;
  }
  std::printf("\n%d 件失敗\n", failures);
  return 1;
}
