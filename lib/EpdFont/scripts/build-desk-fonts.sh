#!/bin/bash
# Desk home fonts: DotGothic16 (SIL OFL) subsets. Clock = digits/colon only;
# text = ASCII plus the characters of the Japanese STR_DESK_* / STR_MOON_* strings.
set -e
cd "$(dirname "$0")"
SRC=../builtinFonts/source/DotGothic16/DotGothic16-Regular.ttf
python fontconvert.py deskclock_56 56 "$SRC" --only-intervals --additional-intervals 0x2D,0x3A > ../builtinFonts/deskclock_56.h
CPS=$(python3 - <<'PY'
import yaml
ja = yaml.safe_load(open('../../I18n/translations/japanese.yaml', encoding='utf-8'))
chars = {chr(c) for c in range(0x20, 0x7F)} | set("年月日（）：・")
for k, v in ja.items():
    if k.startswith(('STR_DESK_', 'STR_MOON_')):
        chars |= set(str(v))
print(' '.join(f'--additional-intervals 0x{ord(c):04X},0x{ord(c):04X}' for c in sorted(chars) if ord(c) >= 0x20))
PY
)
python fontconvert.py desktext_11 11 "$SRC" --only-intervals $CPS > ../builtinFonts/desktext_11.h
python fontconvert.py desktext_15 15 "$SRC" --only-intervals $CPS > ../builtinFonts/desktext_15.h
# Word-of-the-day pronunciation: Noto Sans (SIL OFL) with ASCII + IPA ranges.
python fontconvert.py deskipa_10 10 ../builtinFonts/source/NotoSans/NotoSans-Regular.ttf --only-intervals \
  --additional-intervals 0x20,0x7E --additional-intervals 0xA0,0x17F --additional-intervals 0x250,0x2FF \
  --additional-intervals 0x300,0x36F --additional-intervals 0x3B8,0x3B8 --additional-intervals 0x1D00,0x1DBF \
  > ../builtinFonts/deskipa_10.h
