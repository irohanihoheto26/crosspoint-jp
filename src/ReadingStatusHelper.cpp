#include "ReadingStatusHelper.h"

#include <Arduino.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <esp_task_wdt.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>

namespace {

// progress.bin の形式差（docs/file-formats.md 参照）。
// flagOffset < 0 は読了フラグを持たない形式（TXT）
struct ProgressLayout {
  const char* prefix;
  int flagOffset;
  int percentOffset;
};

bool layoutFor(const std::string& filepath, ProgressLayout& out) {
  if (FsHelpers::hasEpubExtension(filepath)) {
    out = ProgressLayout{"epub_", 6, 7};
  } else if (FsHelpers::hasXtcExtension(filepath)) {
    out = ProgressLayout{"xtc_", 4, 5};
  } else if (FsHelpers::hasTxtExtension(filepath) || FsHelpers::hasMarkdownExtension(filepath)) {
    out = ProgressLayout{"txt_", -1, 4};
  } else {
    return false;
  }
  return true;
}

}  // namespace

ReadingProgress getReadingProgress(const std::string& filepath, const std::string& cacheDir) {
  ReadingProgress result;
  ProgressLayout layout;
  if (!layoutFor(filepath, layout)) {
    return result;
  }

  // progress.bin パスを構築
  std::string progressPath =
      cacheDir + "/" + layout.prefix + std::to_string(FsHelpers::pathHash(filepath)) + "/progress.bin";

  // openFileForRead は exists と open でパス解決を 2 回する。FAT のディレクトリ検索は
  // 線形走査なので、蔵書が数百冊あると 1 冊あたりのコストがそのまま倍になる。
  // 未読の本（progress.bin が無い）は毎回ログも出てしまうため、open だけで判定する。
  FsFile f = Storage.open(progressPath.c_str(), O_RDONLY);
  if (!f) {
    return result;
  }

  // ファイル全体を読み取り（最大 8 バイト: EPUB の進捗率付き形式）
  uint8_t data[8];
  const int bytesRead = f.read(data, sizeof(data));
  f.close();

  if (bytesRead <= 0) {
    return result;
  }

  result.status = ReadingStatus::Reading;
  if (layout.flagOffset >= 0 && bytesRead > layout.flagOffset && data[layout.flagOffset] == 1) {
    result.status = ReadingStatus::Finished;
  }
  if (bytesRead > layout.percentOffset && data[layout.percentOffset] <= 100) {
    result.percent = data[layout.percentOffset];
  } else if (result.status == ReadingStatus::Finished) {
    // 進捗率フィールドが無い旧ファイルでも、読了なら 100% と分かる
    result.percent = 100;
  }
  return result;
}

ReadingStatus getReadingStatus(const std::string& filepath, const std::string& cacheDir) {
  return getReadingProgress(filepath, cacheDir).status;
}

namespace {

// 既に開いているキャッシュディレクトリのハンドルから progress.bin を探して読書状態を返す。
// パス指定で開き直すと FAT のディレクトリ検索がもう一度走るため、
// openNextFile() で相対的にたどる。
ReadingStatus readStatusFromCacheDir(FsFile& bookDir, bool isEpub) {
  // 読了フラグの位置: EPUB=byte6, XTC=byte4
  const int flagOffset = isEpub ? 6 : 4;

  char name[64];
  bookDir.rewindDirectory();
  for (auto child = bookDir.openNextFile(); child; child = bookDir.openNextFile()) {
    child.getName(name, sizeof(name));
    if (strcmp(name, "progress.bin") != 0) {
      child.close();
      // 通常このループは数件で終わるが、エントリ数の多いディレクトリを
      // 掴んだ場合に外側のループまでウォッチドッグを待たせないようにする
      yield();
      esp_task_wdt_reset();
      continue;
    }

    // ファイル全体を読み取り（最大7バイト: EPUB新フォーマット）
    uint8_t data[7] = {0};
    const int bytesRead = child.read(data, sizeof(data));
    child.close();
    if (bytesRead <= 0) {
      return ReadingStatus::Unread;
    }
    return (bytesRead > flagOffset && data[flagOffset] == 1) ? ReadingStatus::Finished : ReadingStatus::Reading;
  }
  return ReadingStatus::Unread;
}

}  // namespace

ReadingStatusIndex::ReadingStatusIndex(const std::string& cacheDir) {
  const uint32_t startMs = millis();
  uint32_t scannedDirs = 0;
  auto root = Storage.open(cacheDir.c_str());
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    return;
  }

  // 蔵書数は事前に分からないため控えめに確保しておき、再確保によるDRAM断片化を抑える
  epubEntries.reserve(32);
  xtcEntries.reserve(8);

  char name[64];
  root.rewindDirectory();
  for (auto entry = root.openNextFile(); entry; entry = root.openNextFile()) {
    if (!entry.isDirectory()) {
      entry.close();
      continue;
    }
    entry.getName(name, sizeof(name));

    bool isEpub;
    const char* digits;
    if (strncmp(name, "epub_", 5) == 0) {
      isEpub = true;
      digits = name + 5;
    } else if (strncmp(name, "xtc_", 4) == 0) {
      isEpub = false;
      digits = name + 4;
    } else {
      entry.close();
      continue;
    }

    char* end = nullptr;
    const unsigned long long parsed = strtoull(digits, &end, 10);
    if (end == digits || *end != '\0') {
      entry.close();
      continue;
    }

    const ReadingStatus status = readStatusFromCacheDir(entry, isEpub);
    scannedDirs++;
    entry.close();

    // 未読はキャッシュが無い場合と同じ扱いなので保持しない（メモリ節約）
    if (status != ReadingStatus::Unread) {
      (isEpub ? epubEntries : xtcEntries).push_back(Entry{static_cast<size_t>(parsed), status});
    }

    yield();
    esp_task_wdt_reset();
  }
  root.close();

  const auto byKey = [](const Entry& a, const Entry& b) { return a.key < b.key; };
  std::sort(epubEntries.begin(), epubEntries.end(), byKey);
  std::sort(xtcEntries.begin(), xtcEntries.end(), byKey);

  LOG_DBG("RSH", "Reading status index: %u epub, %u xtc (scanned %u cache dirs in %lums)",
          static_cast<unsigned>(epubEntries.size()), static_cast<unsigned>(xtcEntries.size()),
          static_cast<unsigned>(scannedDirs), static_cast<unsigned long>(millis() - startMs));
}

ReadingStatus ReadingStatusIndex::lookup(const std::string& filepath) const {
  const bool isEpub = FsHelpers::hasEpubExtension(filepath);
  if (!isEpub && !FsHelpers::hasXtcExtension(filepath)) {
    return ReadingStatus::Unread;
  }
  return lookupIn(isEpub ? epubEntries : xtcEntries, FsHelpers::pathHash(filepath));
}

ReadingStatus ReadingStatusIndex::lookupIn(const std::vector<Entry>& entries, size_t key) {
  const auto it =
      std::lower_bound(entries.begin(), entries.end(), key, [](const Entry& e, size_t k) { return e.key < k; });
  if (it == entries.end() || it->key != key) {
    return ReadingStatus::Unread;
  }
  return it->status;
}

bool markAsFinished(const std::string& filepath, const std::string& cacheDir) {
  const char* prefix;
  bool isEpub;
  if (FsHelpers::hasEpubExtension(filepath)) {
    prefix = "epub_";
    isEpub = true;
  } else if (FsHelpers::hasXtcExtension(filepath)) {
    prefix = "xtc_";
    isEpub = false;
  } else {
    return false;
  }

  const std::string hash = std::to_string(FsHelpers::pathHash(filepath));
  const std::string bookDir = cacheDir + "/" + prefix + hash;
  const std::string progressPath = bookDir + "/progress.bin";

  // 進捗率付き形式で書く: EPUB=8, XTC=6（docs/file-formats.md）
  const size_t recordSize = isEpub ? 8 : 6;
  const size_t flagOffset = isEpub ? 6 : 4;
  const size_t percentOffset = isEpub ? 7 : 5;

  // 既存progress.binを読み込んで読書位置を保持する（なければゼロ初期化）
  uint8_t data[8] = {0};
  FsFile rf;
  if (Storage.openFileForRead("RSH", progressPath, rf)) {
    rf.read(data, recordSize);
    rf.close();
  }
  data[flagOffset] = 1;
  data[percentOffset] = 100;

  // ディレクトリを確保してから書き込む
  Storage.mkdir(cacheDir.c_str());
  Storage.mkdir(bookDir.c_str());

  FsFile wf;
  if (!Storage.openFileForWrite("RSH", progressPath, wf)) {
    LOG_ERR("RSH", "markAsFinished: Could not open %s for write", progressPath.c_str());
    return false;
  }
  wf.write(data, recordSize);
  wf.close();
  LOG_DBG("RSH", "Marked as finished: %s", filepath.c_str());
  return true;
}
