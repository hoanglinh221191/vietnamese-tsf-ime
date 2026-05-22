#include <iostream>
#include <string>
#include <string_view>
#include <cassert>
#include <windows.h>
#include "engine.hpp"
#include "rules.hpp"
#include "speller.hpp"
#include "speller_data.hpp"

using namespace vn_ime::core;

int g_tests_passed = 0;
int g_tests_failed = 0;

std::string to_utf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

void assert_eq(const std::wstring& actual, const std::wstring& expected, const std::string& test_name) {
    if (actual == expected) {
        std::cout << "  [PASS] " << test_name << ": " << to_utf8(actual) << std::endl;
        g_tests_passed++;
    } else {
        std::cout << "  [FAIL] " << test_name
                  << ": expected \"" << to_utf8(expected) << "\", got \"" << to_utf8(actual) << "\"" << std::endl;
        g_tests_failed++;
    }
}

void type_string(Engine& engine, std::wstring_view keys) {
    for (wchar_t c : keys) {
        engine.ProcessKey(c);
    }
}

void test_telex_tones() {
    std::cout << "\nRunning test_telex_tones..." << std::endl;
    Engine engine(InputMethod::Telex);

    // hoáng (sắc)
    engine.Clear();
    type_string(engine, L"hoangs");
    assert_eq(engine.GetDisplayString(), L"hoáng", "hoang + s -> hoáng");

    // hoàng (huyền)
    engine.Clear();
    type_string(engine, L"hoangf");
    assert_eq(engine.GetDisplayString(), L"hoàng", "hoang + f -> hoàng");

    // hoảng (hỏi)
    engine.Clear();
    type_string(engine, L"hoangr");
    assert_eq(engine.GetDisplayString(), L"hoảng", "hoang + r -> hoảng");

    // hoãng (ngã)
    engine.Clear();
    type_string(engine, L"hoangx");
    assert_eq(engine.GetDisplayString(), L"hoãng", "hoang + x -> hoãng");

    // hoạng (nặng)
    engine.Clear();
    type_string(engine, L"hoangj");
    assert_eq(engine.GetDisplayString(), L"hoạng", "hoang + j -> hoạng");

    // hoang (remove tone)
    engine.Clear();
    type_string(engine, L"hoangs");
    engine.ProcessKey(L'z');
    assert_eq(engine.GetDisplayString(), L"hoang", "hoangs + z -> hoang");
}

void test_telex_modifications() {
    std::cout << "\nRunning test_telex_modifications..." << std::endl;
    Engine engine(InputMethod::Telex);

    // a + a -> â
    engine.Clear();
    type_string(engine, L"aa");
    assert_eq(engine.GetDisplayString(), L"â", "a + a -> â");

    // e + e -> ê
    engine.Clear();
    type_string(engine, L"ee");
    assert_eq(engine.GetDisplayString(), L"ê", "e + e -> ê");

    // o + o -> ô
    engine.Clear();
    type_string(engine, L"oo");
    assert_eq(engine.GetDisplayString(), L"ô", "o + o -> ô");

    // d + d -> đ
    engine.Clear();
    type_string(engine, L"dd");
    assert_eq(engine.GetDisplayString(), L"đ", "d + d -> đ");

    // w -> ư
    engine.Clear();
    type_string(engine, L"w");
    assert_eq(engine.GetDisplayString(), L"ư", "w -> ư");

    // uow -> ươ
    engine.Clear();
    type_string(engine, L"uow");
    assert_eq(engine.GetDisplayString(), L"ươ", "uow -> ươ");

    // tuyee + t + s -> tuyết
    engine.Clear();
    type_string(engine, L"tuyeets");
    assert_eq(engine.GetDisplayString(), L"tuyết", "tuyeets -> tuyết");

    // duong -> đường
    // Test typing order dduwongf (u and o horn typed via w after u)
    engine.Clear();
    type_string(engine, L"dduwongf");
    assert_eq(engine.GetDisplayString(), L"đường", "dduwongf -> đường");

    // Test typing order dduongwf (horn typed at the end of the vowel group)
    engine.Clear();
    type_string(engine, L"dduongwf");
    assert_eq(engine.GetDisplayString(), L"đường", "dduongwf -> đường");

    // Free-style modifications
    engine.Clear();
    type_string(engine, L"vietje");
    assert_eq(engine.GetDisplayString(), L"việt", "vietje -> việt");

    engine.Clear();
    type_string(engine, L"vietes");
    assert_eq(engine.GetDisplayString(), L"viết", "vietes -> viết");

    engine.Clear();
    type_string(engine, L"ddere");
    assert_eq(engine.GetDisplayString(), L"để", "ddere -> để");
}

void test_vni() {
    std::cout << "\nRunning test_vni..." << std::endl;
    Engine engine(InputMethod::VNI);

    // hoang + 2 -> hoàng
    engine.Clear();
    type_string(engine, L"hoang2");
    assert_eq(engine.GetDisplayString(), L"hoàng", "hoang + 2 -> hoàng");

    // tuyet + 6 + 1 -> tuyết
    engine.Clear();
    type_string(engine, L"tuyet61");
    assert_eq(engine.GetDisplayString(), L"tuyết", "tuyet + 6 + 1 -> tuyết");

    // d + 9 -> đ
    engine.Clear();
    type_string(engine, L"d9");
    assert_eq(engine.GetDisplayString(), L"đ", "d + 9 -> đ");

    // d + 9 + u + o + n + g + 7 + 2 -> đường
    engine.Clear();
    type_string(engine, L"d9uong72");
    assert_eq(engine.GetDisplayString(), L"đường", "d9uong72 -> đường");
}

void test_backspace_undo() {
    std::cout << "\nRunning test_backspace_undo..." << std::endl;
    Engine engine(InputMethod::Telex);

    // hoáng -> Backspace -> hoang
    engine.Clear();
    type_string(engine, L"hoangs");
    assert_eq(engine.GetDisplayString(), L"hoáng", "Pre-backspace: hoáng");
    
    engine.Backspace();
    assert_eq(engine.GetDisplayString(), L"hoang", "Backspace once -> hoang");

    engine.Backspace();
    assert_eq(engine.GetDisplayString(), L"hoan", "Backspace twice -> hoan");
}

void test_telex_escapes() {
    std::cout << "\nRunning test_telex_escapes..." << std::endl;
    Engine engine(InputMethod::Telex);

    // a + s + s -> as
    engine.Clear();
    type_string(engine, L"ass");
    assert_eq(engine.GetDisplayString(), L"as", "a + s + s -> as");

    // hoang + f + f -> hoangf
    engine.Clear();
    type_string(engine, L"hoangff");
    assert_eq(engine.GetDisplayString(), L"hoangf", "hoang + f + f -> hoangf");
}

void test_english_bypass() {
    std::cout << "\nRunning test_english_bypass..." << std::endl;
    Engine engine(InputMethod::Telex);

    // github -> github (invalid Vietnamese, contains g-i-t-h-u-b)
    engine.Clear();
    type_string(engine, L"github");
    assert_eq(engine.GetDisplayString(), L"github", "github -> github (bypass)");

    // win -> win (contains w, not a Vietnamese word)
    engine.Clear();
    type_string(engine, L"win");
    assert_eq(engine.GetDisplayString(), L"win", "win -> win (bypass)");

    // param -> param
    engine.Clear();
    type_string(engine, L"param");
    assert_eq(engine.GetDisplayString(), L"param", "param -> param (bypass)");
}

void test_speller_corrections() {
    std::cout << "\nRunning test_speller_corrections..." << std::endl;
    Engine engine(InputMethod::Telex);

    // IsInDictionary check directly
    bool is_sorted = true;
    for (size_t i = 1; i < vn_ime::core::speller::DICTIONARY_SIZE; ++i) {
        if (vn_ime::core::speller::DICTIONARY[i] < vn_ime::core::speller::DICTIONARY[i-1]) {
            std::cout << "Dictionary NOT sorted at index " << i << ": " 
                      << to_utf8(std::wstring(vn_ime::core::speller::DICTIONARY[i-1])) << " > " 
                      << to_utf8(std::wstring(vn_ime::core::speller::DICTIONARY[i])) << std::endl;
            is_sorted = false;
            break;
        }
    }
    if (is_sorted) {
        std::cout << "  [PASS] DICTIONARY is sorted" << std::endl;
        g_tests_passed++;
    } else {
        std::cout << "  [FAIL] DICTIONARY is NOT sorted!" << std::endl;
        g_tests_failed++;
    }

    bool is_viet = vn_ime::core::speller::IsInDictionary(L"việt");
    bool is_eng = vn_ime::core::speller::IsInDictionary(L"github");
    if (is_viet && !is_eng) {
        std::cout << "  [PASS] IsInDictionary check: 'việt' is true, 'github' is false" << std::endl;
        g_tests_passed++;
    } else {
        std::cout << "  [FAIL] IsInDictionary check" << std::endl;
        g_tests_failed++;
    }

    // 1. Tone shifting / correction: hòa -> hoà
    engine.Clear();
    type_string(engine, L"hoaf"); // default engine output will be "hòa" (no final consonant, tone on o)
    assert_eq(engine.GetDisplayString(), L"hoà", "hoaf -> hoà (tone shift to matching dictionary syllable)");

    // 2. Typo correction: thuyes -> thuyết
    engine.Clear();
    type_string(engine, L"thuyes");
    assert_eq(engine.GetDisplayString(), L"thuyết", "thuyes -> thuyết (missing t correction)");

    // 3. Typo correction: tuyetn -> tuyến
    engine.Clear();
    type_string(engine, L"tuyetn");
    assert_eq(engine.GetDisplayString(), L"tuyến", "tuyetn -> tuyến (swapped keys/missing tone correction)");

    // 4. Typo correction: dduocj -> được
    engine.Clear();
    type_string(engine, L"dduocj");
    assert_eq(engine.GetDisplayString(), L"được", "dduocj -> được (vowel substitution uo -> ươ)");

    // 5. English bypass: qtr, hng, github, win
    engine.Clear();
    type_string(engine, L"qtr");
    assert_eq(engine.GetDisplayString(), L"qtr", "qtr -> qtr (bypass)");

    engine.Clear();
    type_string(engine, L"hng");
    assert_eq(engine.GetDisplayString(), L"hng", "hng -> hng (bypass)");
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    std::cout << "========================================" << std::endl;
    std::cout << "   RUNNING CORE VIETNAMESE ENGINE TESTS " << std::endl;
    std::cout << "========================================" << std::endl;

    test_telex_tones();
    test_telex_modifications();
    test_vni();
    test_backspace_undo();
    test_telex_escapes();
    test_english_bypass();
    test_speller_corrections();

    std::cout << "\n========================================" << std::endl;
    std::cout << " TESTS SUMMARY: " << std::endl;
    std::cout << "   PASSED: " << g_tests_passed << std::endl;
    std::cout << "   FAILED: " << g_tests_failed << std::endl;
    std::cout << "========================================" << std::endl;

    return g_tests_failed > 0 ? 1 : 0;
}
