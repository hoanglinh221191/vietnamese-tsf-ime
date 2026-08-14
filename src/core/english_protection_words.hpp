#pragma once

#include <cstddef>
#include <string_view>

namespace vn_ime::core::speller::data {

// English words that frequently collide with Telex keys, plus common
// technical acronyms. Exact matches receive strong protection even in the
// Balanced policy.
inline constexpr std::wstring_view STRONG_ENGLISH_PROTECTION_WORDS[] = {
    L"amd", L"apk", L"aws", L"bonus", L"boot", L"brief", L"chef", L"complex",
    L"context", L"cool", L"cow", L"csv", L"deb", L"dna", L"dns", L"door", L"far",
    L"fax", L"feel", L"few", L"floor", L"focus", L"food", L"ftp", L"gcp",
    L"gif", L"gnu", L"green", L"grow", L"gz", L"half", L"hdd", L"http",
    L"ibm", L"ios", L"iso", L"jar", L"keep", L"know", L"lan", L"linux",
    L"low", L"macos", L"meet", L"mit", L"mix", L"mp3", L"mp4", L"need",
    L"nor", L"npm", L"pip", L"pool", L"rar", L"rna", L"room", L"screen", L"self",
    L"sir", L"sleep", L"ssd", L"ssh", L"ssl", L"star", L"status", L"street",
    L"svg", L"tar", L"tcp", L"too", L"udp", L"usb", L"vga", L"virus",
    L"vni", L"vow", L"vpn", L"wan", L"war", L"was", L"wav", L"week",
    L"wifi", L"wood", L"world", L"would", L"wow", L"yes", L"zoom",
};

inline constexpr size_t STRONG_ENGLISH_PROTECTION_WORDS_SIZE =
    sizeof(STRONG_ENGLISH_PROTECTION_WORDS) /
    sizeof(STRONG_ENGLISH_PROTECTION_WORDS[0]);

} // namespace vn_ime::core::speller::data
