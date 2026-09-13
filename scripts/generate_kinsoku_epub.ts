#!/usr/bin/env -S deno run --allow-read --allow-write
// 禁則処理（行頭禁則・行末禁則・分離禁止）の確認用 EPUB を作る。
//
//   deno run -A scripts/generate_kinsoku_epub.ts
//   → test/epubs/ja_kinsoku.epub
//
// 各段落は約物を一定周期で含むので、1 行あたりの文字数が何文字であっても
// どこかの行で「行頭に約物が来る」状態が必ず発生する。修正前後の比較用。

import { resolve } from "jsr:@std/path@1";

const repoRoot = resolve(new URL("..", import.meta.url).pathname);
const outPath = `${repoRoot}/test/epubs/ja_kinsoku.epub`;

// 周期の異なる約物を混ぜて、行幅がいくつでも違反が出るようにする
const sections: { title: string; paras: string[] }[] = [
  {
    title: "一 行頭禁則（句読点）",
    paras: [
      "吾輩は猫である、名前はまだ無い。".repeat(12),
      "どこで生れたか頓と見当がつかぬ、何でも薄暗いじめじめした所で、にゃあにゃあ泣いて居た事丈は記憶して居る。".repeat(4),
    ],
  },
  {
    title: "二 行頭禁則（閉じ括弧）・行末禁則（開き括弧）",
    paras: [
      "「そうですか」と彼は言った。（本当に？）と私は思った。『吾輩は猫である』を読む。［注一］を見よ。".repeat(4),
      "彼は「まさか」と呟き、私は（そんな）と返し、母は『やめて』と叫んだ。".repeat(5),
    ],
  },
  {
    title: "三 行頭禁則（小書き仮名・長音・中点）",
    paras: [
      "しゃっくりがぴたりと止まった、ジューッと焼ける音、ぎゅっと握った手。".repeat(5),
      "山田・鈴木・佐藤の三名、コーヒー・紅茶・ジュースのいずれか、ニューヨーク・パリ・ロンドン。".repeat(4),
    ],
  },
  {
    title: "四 行頭禁則（繰返し記号・ハイフン類）",
    paras: [
      "日々是好日、人々の暮らし、山々の頂、その他いろゝゝの事、ハヽヽと笑う。".repeat(5),
      "東京〜大阪の移動、三〇〇〜四〇〇円、ＡＢＣ〜ＸＹＺの範囲。".repeat(5),
    ],
  },
  {
    title: "五 分離禁止（リーダー・ダッシュ）",
    paras: [
      "そして……彼は――去った。残されたのは沈黙……ただそれだけ――だった。".repeat(5),
      "あのとき‥‥私は何も言えなかった。いまでも――そう、いまでも……悔やんでいる。".repeat(4),
    ],
  },
  {
    title: "六 感嘆符・疑問符",
    paras: [
      "なんだって！ 本当か？ そんな馬鹿な！ ありえない？ いや、確かだ！".repeat(6),
      "待て！　行くな？　止まれ！　答えろ？　聞こえるか！".repeat(6),
    ],
  },
];

function esc(s: string): string {
  return s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
}

const chapters = sections.map((sec, i) => {
  const body = sec.paras.map((p) => `<p>${esc(p)}</p>`).join("\n");
  return {
    id: `chapter${i + 1}`,
    href: `chapter${i + 1}.xhtml`,
    title: sec.title,
    xhtml: `<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="ja" lang="ja">
<head><meta charset="utf-8"/><title>${esc(sec.title)}</title></head>
<body>
<h2>${esc(sec.title)}</h2>
${body}
</body>
</html>
`,
  };
});

const contentOpf = `<?xml version="1.0" encoding="UTF-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="bookid">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
    <dc:identifier id="bookid">urn:uuid:kinsoku-test-0001</dc:identifier>
    <dc:title>禁則処理テスト</dc:title>
    <dc:language>ja</dc:language>
    <dc:creator>CrossPoint Reader</dc:creator>
  </metadata>
  <manifest>
    <item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>
${chapters.map((c) => `    <item id="${c.id}" href="${c.href}" media-type="application/xhtml+xml"/>`).join("\n")}
  </manifest>
  <spine>
${chapters.map((c) => `    <itemref idref="${c.id}"/>`).join("\n")}
  </spine>
</package>
`;

const navXhtml = `<?xml version="1.0" encoding="UTF-8"?>
<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops" xml:lang="ja" lang="ja">
<head><meta charset="utf-8"/><title>目次</title></head>
<body><nav epub:type="toc"><ol>
${chapters.map((c) => `<li><a href="${c.href}">${esc(c.title)}</a></li>`).join("\n")}
</ol></nav></body>
</html>
`;

const containerXml = `<?xml version="1.0" encoding="UTF-8"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
  <rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles>
</container>
`;

// --- ZIP 書き出し（mimetype は無圧縮で先頭に置く） -------------------------
const enc = new TextEncoder();

function crc32(buf: Uint8Array): number {
  let c = ~0;
  for (let i = 0; i < buf.length; i++) {
    c ^= buf[i];
    for (let k = 0; k < 8; k++) c = (c >>> 1) ^ (0xedb88320 & -(c & 1));
  }
  return ~c >>> 0;
}

type Entry = { name: string; data: Uint8Array; store: boolean };
const entries: Entry[] = [
  { name: "mimetype", data: enc.encode("application/epub+zip"), store: true },
  { name: "META-INF/container.xml", data: enc.encode(containerXml), store: false },
  { name: "OEBPS/content.opf", data: enc.encode(contentOpf), store: false },
  { name: "OEBPS/nav.xhtml", data: enc.encode(navXhtml), store: false },
  ...chapters.map((c) => ({ name: `OEBPS/${c.href}`, data: enc.encode(c.xhtml), store: false })),
];

const chunks: Uint8Array[] = [];
const central: Uint8Array[] = [];
let offset = 0;

function u16(v: number) { return new Uint8Array([v & 0xff, (v >> 8) & 0xff]); }
function u32(v: number) { return new Uint8Array([v & 0xff, (v >> 8) & 0xff, (v >> 16) & 0xff, (v >>> 24) & 0xff]); }
function cat(parts: Uint8Array[]): Uint8Array {
  const total = parts.reduce((n, p) => n + p.length, 0);
  const out = new Uint8Array(total);
  let o = 0;
  for (const p of parts) { out.set(p, o); o += p.length; }
  return out;
}

for (const e of entries) {
  const nameBytes = enc.encode(e.name);
  const raw = e.data;
  const method = e.store ? 0 : 8;
  const body = e.store ? raw : new Uint8Array(await new Response(
    new Blob([raw as BlobPart]).stream().pipeThrough(new CompressionStream("deflate-raw")),
  ).arrayBuffer());
  const crc = crc32(raw);
  const local = cat([
    u32(0x04034b50), u16(20), u16(0), u16(method), u16(0), u16(0),
    u32(crc), u32(body.length), u32(raw.length), u16(nameBytes.length), u16(0), nameBytes,
  ]);
  chunks.push(local, body);
  central.push(cat([
    u32(0x02014b50), u16(20), u16(20), u16(0), u16(method), u16(0), u16(0),
    u32(crc), u32(body.length), u32(raw.length), u16(nameBytes.length), u16(0), u16(0), u16(0), u16(0),
    u32(0), u32(offset), nameBytes,
  ]));
  offset += local.length + body.length;
}

const centralBlob = cat(central);
chunks.push(centralBlob);
chunks.push(cat([
  u32(0x06054b50), u16(0), u16(0), u16(entries.length), u16(entries.length),
  u32(centralBlob.length), u32(offset), u16(0),
]));

await Deno.writeFile(outPath, cat(chunks));
console.log(`書き出し: ${outPath} (${entries.length} entries)`);
