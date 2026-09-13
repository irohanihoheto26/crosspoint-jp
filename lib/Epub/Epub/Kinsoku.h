#pragma once

#include <Utf8.h>
#include <VerticalTextUtils.h>

#include <cstdint>
#include <string>
#include <vector>

// 禁則処理（JIS X 4051 / W3C JLREQ）の文字分類。
//
// 横書き（ParsedText::computeLineBreaks）と縦書き（layoutVerticalColumns）の
// 両方から使う。分類は JLREQ の文字クラスに対応させてある:
//
//   行頭禁則 = cl-02 終わり括弧類 / cl-03 ハイフン類 / cl-04 区切り約物 /
//              cl-05 中点類・小書き仮名 / cl-06 句点類 / cl-07 読点類 /
//              cl-09 繰返し記号 / cl-10 長音記号 / cl-12 後置省略記号
//   行末禁則 = cl-01 始め括弧類 / cl-13 前置省略記号
//   分離禁止 = cl-08（同じ文字が連続したものを分割しない）
//
// すべて constexpr 関数（switch）なのでテーブルは Flash に置かれ、DRAM を使わない。
namespace Kinsoku {

// 行頭禁則文字: 行（縦書きでは列）の先頭に置いてはならない。
inline constexpr bool isLineStartProhibited(const uint32_t cp) {
  switch (cp) {
    // cl-06 句点類 / cl-07 読点類
    case 0x3001:  // 、
    case 0x3002:  // 。
    case 0xFF0C:  // ，
    case 0xFF0E:  // ．
    case 0xFF61:  // ｡
    case 0xFF64:  // ､
    case 0x002C:  // ,
    case 0x002E:  // .
    // cl-05 中点類
    case 0x30FB:  // ・
    case 0xFF1A:  // ：
    case 0xFF1B:  // ；
    case 0xFF65:  // ･
    case 0x003A:  // :
    case 0x003B:  // ;
    // cl-04 区切り約物
    case 0xFF01:  // ！
    case 0xFF1F:  // ？
    case 0x203C:  // ‼
    case 0x2047:  // ⁇
    case 0x2048:  // ⁈
    case 0x2049:  // ⁉
    case 0x0021:  // !
    case 0x003F:  // ?
    // cl-02 終わり括弧類
    case 0x3009:  // 〉
    case 0x300B:  // 》
    case 0x300D:  // 」
    case 0x300F:  // 』
    case 0x3011:  // 】
    case 0x3015:  // 〕
    case 0x3017:  // 〗
    case 0x3019:  // 〙
    case 0x301B:  // 〛
    case 0x301F:  // 〟
    case 0x2019:  // ’
    case 0x201D:  // ”
    case 0xFF09:  // ）
    case 0xFF3D:  // ］
    case 0xFF5D:  // ｝
    case 0xFF60:  // ｠
    case 0x00BB:  // »
    case 0x0029:  // )
    case 0x005D:  // ]
    case 0x007D:  // }
    // cl-03 ハイフン類
    case 0x2010:  // ‐
    case 0x2013:  // –
    case 0x301C:  // 〜
    case 0x30A0:  // ゠
    case 0xFF5E:  // ～
    // cl-09 繰返し記号
    case 0x3005:  // 々
    case 0x303B:  // 〻
    case 0x309D:  // ゝ
    case 0x309E:  // ゞ
    case 0x30FD:  // ヽ
    case 0x30FE:  // ヾ
    // cl-10 長音記号
    case 0x30FC:  // ー
    case 0xFF70:  // ｰ
    // 半角カナの濁点・半濁点（前の仮名から離せない）
    case 0xFF9E:  // ﾞ
    case 0xFF9F:  // ﾟ
    // cl-12 後置省略記号（直前の数値に付く）
    case 0x00B0:  // °
    case 0x2030:  // ‰
    case 0x2032:  // ′
    case 0x2033:  // ″
    case 0x2103:  // ℃
    case 0xFF05:  // ％
      return true;
    default:
      break;
  }
  // cl-05 小書き仮名。VerticalTextUtils::isSmallKana() と共有して
  // 2 つのリストが食い違わないようにする（ゎ ヮ ゕ ゖ ヵ ヶ、アイヌ語用小書きカナを含む）。
  return VerticalTextUtils::isSmallKana(cp);
}

// 行末禁則文字: 行（縦書きでは列）の末尾に置いてはならない。
inline constexpr bool isLineEndProhibited(const uint32_t cp) {
  switch (cp) {
    // cl-01 始め括弧類
    case 0x3008:  // 〈
    case 0x300A:  // 《
    case 0x300C:  // 「
    case 0x300E:  // 『
    case 0x3010:  // 【
    case 0x3014:  // 〔
    case 0x3016:  // 〖
    case 0x3018:  // 〘
    case 0x301A:  // 〚
    case 0x301D:  // 〝
    case 0x2018:  // ‘
    case 0x201C:  // “
    case 0xFF08:  // （
    case 0xFF3B:  // ［
    case 0xFF5B:  // ｛
    case 0xFF5F:  // ｟
    case 0x00AB:  // «
    case 0x0028:  // (
    case 0x005B:  // [
    case 0x007B:  // {
    // cl-13 前置省略記号（直後の数値に付く）
    case 0x0024:  // $
    case 0x00A3:  // £
    case 0x00A5:  // ¥
    case 0x20AC:  // €
    case 0x2116:  // №
    case 0x3012:  // 〒
    case 0xFFE5:  // ￥
      return true;
    default:
      return false;
  }
}

// cl-08 分離禁止文字: 同じ文字が連続したときに、その間で分割してはならない。
// 「……」「――」を 1 文字ずつ泣き別れさせないためのもの。
inline constexpr bool isInseparable(const uint32_t cp) {
  switch (cp) {
    case 0x2014:  // —
    case 0x2015:  // ―
    case 0x2025:  // ‥
    case 0x2026:  // …
    case 0x3033:  // 〳
    case 0x3034:  // 〴
    case 0x3035:  // 〵
      return true;
    default:
      return false;
  }
}

// 直前の文字 tail と直後の文字 head の間で分割してはならないか。
inline constexpr bool isInseparablePair(const uint32_t tail, const uint32_t head) {
  return tail == head && isInseparable(tail);
}

// 語の最初の表示文字（先頭のソフトハイフンは飛ばす）。
inline uint32_t firstCodepoint(const std::string& word) {
  const auto* ptr = reinterpret_cast<const unsigned char*>(word.c_str());
  while (true) {
    const uint32_t cp = utf8NextCodepoint(&ptr);
    if (cp == 0) return 0;
    if (cp != 0x00AD) return cp;  // skip soft hyphens
  }
}

// 語の最後の文字。末尾から UTF-8 の先頭バイトまで戻って読む。
inline uint32_t lastCodepoint(const std::string& word) {
  if (word.empty()) return 0;
  // UTF-8 continuation bytes start with 10xxxxxx; scan backward to find the leading byte.
  size_t i = word.size() - 1;
  while (i > 0 && (static_cast<uint8_t>(word[i]) & 0xC0) == 0x80) {
    --i;
  }
  const auto* ptr = reinterpret_cast<const unsigned char*>(word.c_str() + i);
  return utf8NextCodepoint(&ptr);
}

// 追い出しで戻せる最大語数。約物が続く行（「……」「）。」など）でも数語で収束するが、
// 収束しない場合に行が空になるのを防ぐための上限。
inline constexpr int MAX_PULLBACK = 6;

// 行（縦書きでは列）の分割位置 breakAt（行末の次の語の添字）を、禁則規則を満たす位置まで
// 前へ「追い出す」。JIS X 4051 の追い出し処理にあたる。
//
// 見るのは 3 つ:
//   - words[breakAt] の先頭文字が行頭禁則か（次行の先頭に来てはいけない文字か）
//   - words[breakAt - 1] の末尾文字が行末禁則か（この行の末尾に来てはいけない文字か）
//   - その 2 文字が分離禁止の組（「……」のような同字の連続）か
// さらに continuesVec[breakAt]（前の語にスペース無しで続く語）の途中でも切らない。
//
// MAX_PULLBACK 回で収束しない場合は breakAt をそのまま返す。約物だけで埋まった行を
// 無理に追い出すと行が空になり、レイアウトが進まなくなるため。
inline size_t adjustBreak(const std::vector<std::string>& words, const std::vector<bool>& continuesVec,
                          const size_t breakAt, const size_t lineStart) {
  // breakAt == words.size() は段落の末尾。次の行が無いので追い出す意味がない。
  if (breakAt >= words.size() || breakAt <= lineStart + 1) return breakAt;

  size_t b = breakAt;
  for (int i = 0; i < MAX_PULLBACK; ++i) {
    const uint32_t head = firstCodepoint(words[b]);
    const uint32_t tail = lastCodepoint(words[b - 1]);
    const bool splitsWord = b < continuesVec.size() && continuesVec[b];
    if (!splitsWord && !isLineStartProhibited(head) && !isLineEndProhibited(tail) && !isInseparablePair(tail, head)) {
      return b;
    }
    if (b <= lineStart + 1) break;  // これ以上戻すと行が空になる
    --b;
  }
  return breakAt;
}

}  // namespace Kinsoku
