#!/usr/bin/env python3
"""Generate Neokey's Vietnamese syllable-pair header.

Both halves of every pair are syllables DICTIONARY already holds, so a pair is
stored as two 16-bit positions in it rather than as text. At thirty thousand
pairs that is the difference between 88 KB of .rdata and a megabyte of strings
plus the heap to index them, and the DLL is loaded into every process that
takes text.

Two indexes are emitted because the two callers ask opposite questions.
Segmentation knows the second syllable of a candidate split and wants the
first ones that can precede it; a membership test knows both. Each index is a
start offset per dictionary entry plus one flat run of the other half, so a
lookup is an array subscript and then a binary search over a short run - no
hashing, no allocation, nothing built at startup.

Pairs whose halves are not both in the dictionary are dropped rather than
approximated: the dictionary is the language's phonotactics, and a pair it
cannot express is a pair this table has no way to look up.
"""

from __future__ import annotations

import argparse
import hashlib
import re
import sys
import unicodedata
from pathlib import Path

ENTRY = re.compile(r'L"([^"\\]*)"')

# What this guards against is a parse that half worked - a dictionary read in
# the wrong encoding, a source file in the wrong column order - because the
# result is a table that looks plausible and segments worse than the one it
# replaced. That is a question about the SHARE of the file that survived, not
# about the size of the table: a deliberately small table is a legitimate
# choice, and at one point an absolute floor written for a large one refused to
# generate the small ones being measured against it.
MIN_SURVIVING_SHARE = 0.9
MIN_EXPECTED_PAIRS = 500


def read_dictionary_order(path: Path) -> list[str]:
    text = path.read_text(encoding="utf-8")
    words = [
        unicodedata.normalize("NFC", match.group(1))
        for match in ENTRY.finditer(text)
        if match.group(1)
    ]
    if not words:
        raise ValueError(f"{path}: no dictionary entries parsed")
    if len(words) != len(set(words)):
        raise ValueError(f"{path}: dictionary has duplicate entries")
    if words != sorted(words):
        raise ValueError(f"{path}: dictionary is not sorted")
    return words


def read_pairs(path: Path) -> list[tuple[str, str]]:
    """Read '<count>\\t<first> <second>' lines, most frequent first."""
    pairs: list[tuple[str, str]] = []
    seen: set[tuple[str, str]] = set()
    for line_number, raw_line in enumerate(
        path.read_text(encoding="utf-8").splitlines(), start=1
    ):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split("\t")
        if len(parts) != 2:
            raise ValueError(
                f"{path}:{line_number}: expected '<count>\\t<first> <second>'")
        count_text, phrase = parts
        if not count_text.isdigit():
            raise ValueError(f"{path}:{line_number}: count must be a number")
        halves = unicodedata.normalize("NFC", phrase).split(" ")
        if len(halves) != 2 or not all(halves):
            raise ValueError(
                f"{path}:{line_number}: expected exactly two syllables")
        key = (halves[0], halves[1])
        if key in seen:
            raise ValueError(f"{path}:{line_number}: duplicate pair {phrase!r}")
        seen.add(key)
        pairs.append(key)
    return pairs


def tone_placement_variants(word: str) -> list[str]:
    """The same syllable with its tone written on a different vowel.

    Vietnamese writes the tone of an oa/oe/uy cluster in two conventions: hoà
    and hòa are one word, spelled two ways. DICTIONARY holds one of them and
    the engine produces that one, so a pair written the other way can never
    match anything and is silently dead. The hand-picked table this replaces
    is written the other way throughout - hòa bình, thủy chung, ủy ban - so
    those entries have never matched a single keystroke.

    Only the placement moves. No tone is added, removed or changed, so a
    variant is the same word or it is nothing.
    """
    decomposed = unicodedata.normalize("NFD", word)
    marks = [c for c in decomposed if unicodedata.combining(c)]
    if len(marks) != 1:
        return []
    bare = "".join(c for c in decomposed if not unicodedata.combining(c))
    mark = marks[0]
    variants = []
    for index in range(len(bare)):
        candidate = unicodedata.normalize(
            "NFC", bare[:index + 1] + mark + bare[index + 1:])
        if candidate != word:
            variants.append(candidate)
    return variants


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def emit_array(out, kind: str, name: str, values: list[int], per_line: int):
    out.write(f"inline constexpr std::array<{kind}, {len(values)}>\n")
    out.write(f"    {name}{{\n")
    for start in range(0, len(values), per_line):
        chunk = values[start:start + per_line]
        out.write("    " + ", ".join(str(v) for v in chunk) + ",\n")
    out.write("};\n\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dictionary", type=Path,
                        default=Path("src/core/speller_data.hpp"))
    parser.add_argument("--pairs", type=Path,
                        default=Path("data/vietnamese_bigrams.txt"))
    parser.add_argument("--output", type=Path,
                        default=Path("src/core/vietnamese_bigrams_generated.hpp"))
    args = parser.parse_args()

    dictionary = read_dictionary_order(args.dictionary)
    position = {word: index for index, word in enumerate(dictionary)}
    pairs = read_pairs(args.pairs)

    def resolve(half: str) -> int | None:
        """The dictionary position of a syllable, ignoring case."""
        lowered = half.lower()
        found = position.get(lowered)
        if found is not None:
            return found
        # Accept the other tone placement only when it is unambiguous.
        matches = [position[v] for v in tone_placement_variants(lowered)
                   if v in position]
        return matches[0] if len(matches) == 1 else None

    usable: list[tuple[int, int]] = []
    dropped: list[tuple[str, str]] = []
    # A pair whose canonical spelling carries a capital. DICTIONARY is
    # lowercase, so the indices cannot hold this and it is kept beside them.
    # There are about twenty - Sài Gòn, Việt Nam, sông Hồng - and segmentation
    # has always emitted them capitalised, so dropping it would quietly turn
    # "vietnam" into "việt nam".
    capitalised: dict[tuple[int, int], int] = {}
    seen_indices: set[tuple[int, int]] = set()
    remapped = 0
    for first, second in pairs:
        left, right = resolve(first), resolve(second)
        if left is None or right is None:
            dropped.append((first, second))
            continue
        if dictionary[left] != first.lower() or dictionary[right] != second.lower():
            remapped += 1
        # Remapping and case-folding can both collide with a pair already read;
        # the table is a set, so keep the first.
        if (left, right) in seen_indices:
            continue
        seen_indices.add((left, right))
        usable.append((left, right))
        mask = (1 if first[:1].isupper() else 0) | (2 if second[:1].isupper() else 0)
        if mask:
            capitalised[(left, right)] = mask

    survived = len(usable) / len(pairs) if pairs else 0.0
    if len(usable) < MIN_EXPECTED_PAIRS or survived < MIN_SURVIVING_SHARE:
        print(f"refusing to generate: {len(usable)} of {len(pairs)} pairs "
              f"survived ({survived:.1%}), below {MIN_SURVIVING_SHARE:.0%} or "
              f"{MIN_EXPECTED_PAIRS} entries. A table built from a parse that "
              f"half worked looks plausible and segments worse than none.",
              file=sys.stderr)
        return 2

    size = len(dictionary)

    def build_index(key_first: bool) -> tuple[list[int], list[int]]:
        buckets: list[list[int]] = [[] for _ in range(size)]
        for first, second in usable:
            key, value = (first, second) if key_first else (second, first)
            buckets[key].append(value)
        starts: list[int] = []
        flat: list[int] = []
        for bucket in buckets:
            starts.append(len(flat))
            # Sorted so the lookup can binary search the run.
            flat.extend(sorted(bucket))
        starts.append(len(flat))
        return starts, flat

    first_starts, seconds = build_index(key_first=True)
    second_starts, firsts = build_index(key_first=False)

    if max(seconds, default=0) > 0xFFFF or max(firsts, default=0) > 0xFFFF:
        raise ValueError("a dictionary position does not fit in uint16")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8", newline="\n") as out:
        out.write(f"// Generated by tools/{Path(__file__).name}\n")
        out.write(f"// from {args.pairs.as_posix()}\n")
        out.write(f"// against {args.dictionary.as_posix()}\n")
        out.write(f"// Source SHA-256: {sha256(args.pairs)}\n")
        out.write(f"// Dictionary SHA-256: {sha256(args.dictionary)}\n")
        out.write("// Do not edit this file manually.\n")
        out.write("#pragma once\n\n")
        out.write("#include <array>\n#include <cstddef>\n#include <cstdint>\n\n")
        out.write("namespace vn_ime::core::speller::data {\n\n")
        out.write("// A pair is two positions in DICTIONARY. Each index below is\n"
                  "// a start offset per dictionary entry, one longer than the\n"
                  "// dictionary so entry n occupies [starts[n], starts[n + 1]),\n"
                  "// plus one flat sorted run of the other half.\n")
        out.write(f"inline constexpr size_t kBigramCount = {len(usable)};\n")
        out.write(f"inline constexpr size_t kBigramDictionarySize = {size};\n\n")

        out.write("// Given the first syllable, the second ones that follow it.\n")
        emit_array(out, "uint32_t", "kBigramFirstStarts", first_starts, 16)
        emit_array(out, "uint16_t", "kBigramSeconds", seconds, 16)

        out.write("// Given the second syllable, the first ones that precede it.\n"
                  "// Segmentation asks this way: it has a candidate second half\n"
                  "// and wants to know what could come before it.\n")
        emit_array(out, "uint32_t", "kBigramSecondStarts", second_starts, 16)
        emit_array(out, "uint16_t", "kBigramFirsts", firsts, 16)

        out.write("// The few pairs written with a capital. DICTIONARY is\n"
                  "// lowercase, so the indices above cannot carry this. Sorted\n"
                  "// by (first << 16) | second; the mask is bit 0 for the first\n"
                  "// syllable and bit 1 for the second, so \"song Hong\" with its\n"
                  "// river capitalised is 2 and \"Viet Nam\" is 3.\n")
        keyed = sorted(((first << 16) | second, mask)
                       for (first, second), mask in capitalised.items())
        out.write(f"inline constexpr size_t kBigramCapitalisedCount = {len(keyed)};\n")
        emit_array(out, "uint32_t", "kBigramCapitalisedPairs",
                   [key for key, _ in keyed], 8)
        emit_array(out, "uint8_t", "kBigramCapitalisedMasks",
                   [mask for _, mask in keyed], 16)

        out.write("} // namespace vn_ime::core::speller::data\n")

    occupied = sum(1 for i in range(size)
                   if first_starts[i + 1] > first_starts[i])
    largest = max((first_starts[i + 1] - first_starts[i] for i in range(size)),
                  default=0)
    bytes_total = (len(first_starts) + len(second_starts)) * 4 + \
                  (len(seconds) + len(firsts)) * 2
    print(f"dictionary entries      : {size:,}")
    print(f"pairs read              : {len(pairs):,}")
    print(f"pairs usable            : {len(usable):,}")
    if remapped:
        print(f"  tone placement moved  : {remapped:,} "
              f"(written in the other convention, dead as they stood)")
    if dropped:
        print(f"  dropped, half unknown : {len(dropped):,} "
              f"({', '.join(a + ' ' + b for a, b in dropped[:6])})")
    print(f"first syllables in use  : {occupied:,}")
    print(f"largest run             : {largest:,}")
    print(f"table size              : {bytes_total / 1024:.0f} KB")
    print(f"written                 : {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
