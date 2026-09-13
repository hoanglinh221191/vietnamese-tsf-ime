#!/usr/bin/env python3
"""Count Vietnamese syllable-pair frequencies from a Wikipedia dump prefix.

The splitter's job is to decide where one syllable ends and the next begins in
a run of keys, so what it needs is not a list of phrases somebody thought of
but the pairs the language actually puts next to each other, in order.

Two rules keep the table honest:

  Both halves must be syllables Neokey already ships. The dictionary is the
  language's phonotactics, so this keeps foreign words, transliterations and
  markup debris out without any judgement about the corpus.

  The pair must be genuinely adjacent - separated by spaces and nothing else.
  Crossing a comma or a full stop invents a collocation that never occurred,
  and a segmentation table built from those would split on evidence that is
  not there. This is the one thing the syllable count did not have to care
  about, and it is why this cannot reuse that script's tokeniser.

The input is a truncated bz2 stream on purpose; the decompressor is expected
to raise at the cut.
"""

from __future__ import annotations

import bz2
import re
import sys
import unicodedata
from collections import Counter
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

DICTIONARY_HEADER = Path(sys.argv[1])
DUMP_PREFIX = Path(sys.argv[2])
OUTPUT = Path(sys.argv[3])
WANTED = int(sys.argv[4]) if len(sys.argv) > 4 else 30000

ENTRY = re.compile(r'L"([^"\\]*)"')
ESCAPE = re.compile(r"\\u([0-9a-fA-F]{4})")
TAG = re.compile(r"<[^>]{0,200}>")
TEMPLATE = re.compile(r"\{\{[^{}]{0,400}\}\}")
LINK_TARGET = re.compile(r"\[\[(?:[^\]|]{0,200}\|)?")
# A letter run, or the gap before it. Keeping the gap is the whole point: a
# pair only counts when the gap between its halves is blank.
TOKEN = re.compile(r"([^\W\d_]+)|([^\w]+|[\d_]+)", re.UNICODE)

MIN_EXPECTED_SYLLABLES = 5000


def load_syllables(path: Path) -> set[str]:
    text = path.read_text(encoding="utf-8")
    words: set[str] = set()
    for match in ENTRY.finditer(text):
        raw = ESCAPE.sub(lambda m: chr(int(m.group(1), 16)), match.group(1))
        word = unicodedata.normalize("NFC", raw)
        if word:
            words.add(word)
    return words


def main() -> int:
    syllables = load_syllables(DICTIONARY_HEADER)
    accented = sum(1 for w in syllables if not w.isascii())
    print(f"syllables loaded from the shipped dictionary: {len(syllables)}")
    print(f"  of which carry a diacritic: {accented}")
    if len(syllables) < MIN_EXPECTED_SYLLABLES or accented == 0:
        print("refusing to count: the dictionary did not parse. A partial set "
              "would produce a table that looks plausible and is wrong.")
        return 2

    pairs: Counter[tuple[str, str]] = Counter()
    singles: Counter[str] = Counter()
    total_tokens = 0
    matched_tokens = 0
    adjacent_pairs = 0
    decompressed = 0

    decompressor = bz2.BZ2Decompressor()
    carry = ""
    # The previous Vietnamese syllable, or None when the run was broken by
    # punctuation, a digit, a foreign word or a chunk boundary.
    previous: str | None = None

    with DUMP_PREFIX.open("rb") as handle:
        while True:
            chunk = handle.read(8 << 20)
            if not chunk:
                break
            try:
                raw = decompressor.decompress(chunk)
            except (OSError, EOFError, ValueError):
                break  # the cut mid-block, which is expected
            if not raw:
                continue
            decompressed += len(raw)
            text = carry + raw.decode("utf-8", errors="ignore")
            carry = text[-64:]
            text = text[:-64] if len(text) > 64 else text

            text = TAG.sub(" ", text)
            text = TEMPLATE.sub(" ", text)
            text = LINK_TARGET.sub(" ", text)

            # A chunk boundary is not evidence of adjacency either.
            previous = None

            for word_match, gap_match in TOKEN.findall(text):
                if gap_match:
                    # Only blank space keeps a pair together. Anything else -
                    # comma, full stop, bracket, digit - ends the run.
                    if not gap_match.isspace() or "\n" in gap_match:
                        previous = None
                    continue

                total_tokens += 1
                word = word_match.lower()
                if word not in syllables:
                    if word.isascii():
                        previous = None
                        continue
                    word = unicodedata.normalize("NFC", word)
                    if word not in syllables:
                        previous = None
                        continue
                matched_tokens += 1
                singles[word] += 1
                if previous is not None:
                    pairs[(previous, word)] += 1
                    adjacent_pairs += 1
                previous = word

    print(f"decompressed text            : {decompressed / (1 << 20):,.0f} MB")
    print(f"letter tokens seen           : {total_tokens:,}")
    print(f"tokens that are Vietnamese   : {matched_tokens:,} "
          f"({100.0 * matched_tokens / max(total_tokens, 1):.1f}%)")
    print(f"adjacent Vietnamese pairs    : {adjacent_pairs:,}")
    print(f"distinct pairs seen          : {len(pairs):,}")

    if len(pairs) < WANTED:
        print(f"only {len(pairs):,} distinct pairs, fewer than the {WANTED:,} asked for")

    top = pairs.most_common(WANTED)
    covered = sum(count for _, count in top)
    print(f"top {len(top):,} pairs cover       : {covered:,} of {adjacent_pairs:,} "
          f"occurrences ({100.0 * covered / max(adjacent_pairs, 1):.1f}%)")

    with OUTPUT.open("w", encoding="utf-8") as out:
        for (left, right), count in top:
            out.write(f"{count}\t{left} {right}\n")
    print(f"written                      : {OUTPUT}")

    print("\ncoverage as the table grows")
    running = 0
    marks = [1000, 2038, 5000, 10000, 15000, 20000, 25000, 30000]
    ordered = pairs.most_common()
    mark_index = 0
    for index, (_, count) in enumerate(ordered, start=1):
        running += count
        while mark_index < len(marks) and index == marks[mark_index]:
            print(f"  top {marks[mark_index]:>6,} pairs : "
                  f"{100.0 * running / max(adjacent_pairs, 1):5.1f}% of running text")
            mark_index += 1
        if mark_index >= len(marks):
            break

    print("\ntop 20 pairs")
    for (left, right), count in top[:20]:
        print(f"  {count:>9,}  {left} {right}")

    print("\nthe pairs the splitter cannot do today")
    for probe in ("chúng tôi", "cuộc sống", "việt nam", "không biết",
                  "bởi vì", "hôm nay"):
        left, right = probe.split()
        print(f"  {probe:<14} {pairs.get((left, right), 0):>9,}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
