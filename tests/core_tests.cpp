#include <iostream>
#include <string>
#include <string_view>
#include <cassert>
#include <vector>
#include <windows.h>
#include "engine.hpp"
#include "rules.hpp"
#include "speller.hpp"
#include "speller_data.hpp"
#include "config.hpp"

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

void assert_engine_output(InputMethod method, std::wstring_view keys, const std::wstring& expected, const std::string& test_name) {
    Engine engine(method);
    type_string(engine, keys);
    assert_eq(engine.GetDisplayString(), expected, test_name);
}

std::wstring type_text_committing_on_spaces(InputMethod method, std::wstring_view keys) {
    Engine engine(method);
    std::wstring output;

    for (wchar_t c : keys) {
        if (c == L' ') {
            output += engine.GetDisplayString();
            output.push_back(L' ');
            engine.Clear();
        } else {
            engine.ProcessKey(c);
        }
    }

    output += engine.GetDisplayString();
    return output;
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

    // roi62 -> rồi (free-style modifier circumflex + tone)
    engine.Clear();
    type_string(engine, L"roi62");
    assert_eq(engine.GetDisplayString(), L"rồi", "roi62 -> rồi");

    // dong9 -> đong
    engine.Clear();
    type_string(engine, L"dong9");
    assert_eq(engine.GetDisplayString(), L"đong", "dong9 -> đong");

    // dong69 -> đông (free-style d-bar + circumflex)
    engine.Clear();
    type_string(engine, L"dong69");
    assert_eq(engine.GetDisplayString(), L"đông", "dong69 -> đông");

    // d9ong6 -> đông
    engine.Clear();
    type_string(engine, L"d9ong6");
    assert_eq(engine.GetDisplayString(), L"đông", "d9ong6 -> đông");

    // a68 -> ă (override circumflex with breve)
    engine.Clear();
    type_string(engine, L"a68");
    assert_eq(engine.GetDisplayString(), L"ă", "a68 -> ă");

    // a86 -> â (override breve with circumflex)
    engine.Clear();
    type_string(engine, L"a86");
    assert_eq(engine.GetDisplayString(), L"â", "a86 -> â");
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

    // Test dduwocj -> được
    engine.Clear();
    type_string(engine, L"dduwocj");
    assert_eq(engine.GetDisplayString(), L"được", "dduwocj -> được");

    // Test casing: Dduocj -> Được
    engine.Clear();
    type_string(engine, L"Dduocj");
    assert_eq(engine.GetDisplayString(), L"Được", "Dduocj -> Được");

    // Test casing: DDUOCJ -> ĐƯỢC
    engine.Clear();
    type_string(engine, L"DDUOCJ");
    assert_eq(engine.GetDisplayString(), L"ĐƯỢC", "DDUOCJ -> ĐƯỢC");

    // 5. English bypass: qtr, hng, github, win
    engine.Clear();
    type_string(engine, L"qtr");
    assert_eq(engine.GetDisplayString(), L"qtr", "qtr -> qtr (bypass)");

    engine.Clear();
    type_string(engine, L"hng");
    assert_eq(engine.GetDisplayString(), L"hng", "hng -> hng (bypass)");
}

void assert_true(bool condition, const std::string& test_name) {
    if (condition) {
        std::cout << "  [PASS] " << test_name << std::endl;
        g_tests_passed++;
    } else {
        std::cout << "  [FAIL] " << test_name << ": expected true, got false" << std::endl;
        g_tests_failed++;
    }
}

void test_reconversion_helpers() {
    std::cout << "\nRunning test_reconversion_helpers..." << std::endl;

    // Test IsToneKey
    assert_true(rules::IsToneKey(L's', InputMethod::Telex), "IsToneKey(s, Telex)");
    assert_true(rules::IsToneKey(L'f', InputMethod::Telex), "IsToneKey(f, Telex)");
    assert_true(rules::IsToneKey(L'r', InputMethod::Telex), "IsToneKey(r, Telex)");
    assert_true(rules::IsToneKey(L'x', InputMethod::Telex), "IsToneKey(x, Telex)");
    assert_true(rules::IsToneKey(L'j', InputMethod::Telex), "IsToneKey(j, Telex)");
    assert_true(rules::IsToneKey(L'z', InputMethod::Telex), "IsToneKey(z, Telex)");
    assert_true(!rules::IsToneKey(L'a', InputMethod::Telex), "!IsToneKey(a, Telex)");
    
    assert_true(rules::IsToneKey(L'1', InputMethod::VNI), "IsToneKey(1, VNI)");
    assert_true(rules::IsToneKey(L'2', InputMethod::VNI), "IsToneKey(2, VNI)");
    assert_true(rules::IsToneKey(L'3', InputMethod::VNI), "IsToneKey(3, VNI)");
    assert_true(rules::IsToneKey(L'4', InputMethod::VNI), "IsToneKey(4, VNI)");
    assert_true(rules::IsToneKey(L'5', InputMethod::VNI), "IsToneKey(5, VNI)");
    assert_true(rules::IsToneKey(L'0', InputMethod::VNI), "IsToneKey(0, VNI)");
    assert_true(!rules::IsToneKey(L'6', InputMethod::VNI), "!IsToneKey(6, VNI)");

    // Test IsWordChar
    assert_true(rules::IsWordChar(L'a'), "IsWordChar(a)");
    assert_true(rules::IsWordChar(L'đ'), "IsWordChar(đ)");
    assert_true(rules::IsWordChar(L'ư'), "IsWordChar(ư)");
    assert_true(!rules::IsWordChar(L' '), "!IsWordChar(space)");
    assert_true(!rules::IsWordChar(L'.'), "!IsWordChar(dot)");

    // Test ReconstructRawKeys - Telex
    assert_eq(rules::ReconstructRawKeys(L"hoang", InputMethod::Telex), L"hoang", "ReconstructRawKeys: hoang");
    assert_eq(rules::ReconstructRawKeys(L"hoàng", InputMethod::Telex), L"hoangf", "ReconstructRawKeys: hoàng -> hoangf");
    assert_eq(rules::ReconstructRawKeys(L"đường", InputMethod::Telex), L"duongdwf", "ReconstructRawKeys: đường -> duongdwf");
    assert_eq(rules::ReconstructRawKeys(L"Đường", InputMethod::Telex), L"Duongdwf", "ReconstructRawKeys: Đường -> Duongdwf");
    
    // Test ReconstructRawKeys - VNI
    assert_eq(rules::ReconstructRawKeys(L"hoàng", InputMethod::VNI), L"hoang2", "ReconstructRawKeys: hoàng VNI -> hoang2");
    assert_eq(rules::ReconstructRawKeys(L"đường", InputMethod::VNI), L"duong972", "ReconstructRawKeys: đường VNI -> duong972");
}

void test_golden_corpus() {
    std::cout << "\nRunning test_golden_corpus..." << std::endl;

    struct CorpusCase {
        InputMethod method;
        std::wstring_view keys;
        std::wstring expected;
        const char* name;
    };

    const std::vector<CorpusCase> cases = {
        {InputMethod::SimpleTelex, L"hoangs", L"ho\u00E1ng", "SimpleTelex: hoangs -> hoang acute"},
        {InputMethod::SimpleTelex, L"dduongwf", L"\u0111\u01B0\u1EDDng", "SimpleTelex: dduongwf -> duong"},
        {InputMethod::SimpleTelex, L"vietes", L"vi\u1EBFt", "SimpleTelex: vietes -> viet acute"},
        {InputMethod::Telex, L"cmd.exe", L"cmd.exe", "Punctuation bypass: cmd.exe"},
        {InputMethod::Telex, L"name@", L"name@", "Special char bypass: name@"},
        {InputMethod::Telex, L"vietes.", L"vietes.", "Core punctuation remains raw; TSF commits punctuation"},
        {InputMethod::Telex, L"github", L"github", "English mixed: github"},
        {InputMethod::Telex, L"CMake", L"CMake", "English mixed uppercase: CMake"},
        {InputMethod::Telex, L"Vietes", L"Vi\u1EBFt", "Uppercase mixed: Vietes -> Viet"},
        {InputMethod::Telex, L"HOANGF", L"HO\u00C0NG", "Uppercase mixed: HOANGF -> HOANG grave"},
        {InputMethod::VNI, L"Viet61", L"Vi\u1EBFt", "VNI uppercase mixed: Viet61 -> Viet"},
    };

    for (const auto& c : cases) {
        assert_engine_output(c.method, c.keys, c.expected, c.name);
    }

    assert_eq(type_text_committing_on_spaces(InputMethod::Telex, L"vietes nam"), L"vi\u1EBFt nam", "Multi-word: vietes nam");
    assert_eq(type_text_committing_on_spaces(InputMethod::Telex, L"github vietes"), L"github vi\u1EBFt", "Multi-word mixed English/Vietnamese");
}

void test_reconversion_ad_hoc_corpus() {
    std::cout << "\nRunning test_reconversion_ad_hoc_corpus..." << std::endl;

    auto preview_reconversion = [](std::wstring_view committed_word, wchar_t key, InputMethod method) {
        std::wstring raw = rules::ReconstructRawKeys(committed_word, method);
        raw.push_back(key);

        Engine engine(method);
        type_string(engine, raw);
        return engine.GetDisplayString();
    };

    assert_eq(preview_reconversion(L"hoang", L's', InputMethod::Telex), L"ho\u00E1ng", "Ad-hoc reconversion: hoang + s");
    assert_eq(preview_reconversion(L"hoang", L'f', InputMethod::Telex), L"ho\u00E0ng", "Ad-hoc reconversion: hoang + f");
    assert_eq(preview_reconversion(L"ho\u00E0ng", L's', InputMethod::Telex), L"ho\u00E1ng", "Ad-hoc reconversion: hoang grave + s");
    assert_eq(preview_reconversion(L"duong", L'w', InputMethod::Telex), L"d\u01B0\u01A1ng", "Ad-hoc reconversion: duong + w");
    assert_eq(preview_reconversion(L"hoang", L'1', InputMethod::VNI), L"ho\u00E1ng", "Ad-hoc reconversion VNI: hoang + 1");
}

void test_reconstruct_roundtrip_corpus() {
    std::cout << "\nRunning test_reconstruct_roundtrip_corpus..." << std::endl;

    assert_eq(rules::ReconstructRawKeys(L"vi\u1EBFt", InputMethod::Telex), L"vietes", "Roundtrip raw Telex: viet");
    assert_eq(rules::ReconstructRawKeys(L"Vi\u1EBFt", InputMethod::Telex), L"Vietes", "Roundtrip raw Telex: Viet");
    assert_eq(rules::ReconstructRawKeys(L"\u0111\u01B0\u1EE3c", InputMethod::Telex), L"duocdwj", "Roundtrip raw Telex: duoc");
    assert_eq(rules::ReconstructRawKeys(L"vi\u1EBFt", InputMethod::VNI), L"viet61", "Roundtrip raw VNI: viet");
    assert_eq(rules::ReconstructRawKeys(L"\u0111\u01B0\u1EE3c", InputMethod::VNI), L"duoc975", "Roundtrip raw VNI: duoc");
}

void test_app_blocklist_config_helpers() {
    std::cout << "\nRunning test_app_blocklist_config_helpers..." << std::endl;

    assert_eq(vn_ime::NormalizeProcessName(L"notepad++.exe"), L"notepad++.exe", "Blocklist normalize: bare name");
    assert_eq(vn_ime::NormalizeProcessName(L" C:\\Path\\Notepad++.EXE "), L"notepad++.exe", "Blocklist normalize: path trim lower");
    assert_eq(vn_ime::NormalizeProcessName(L"\"C:\\Tools\\WindowsTerminal.exe\""), L"windowsterminal.exe", "Blocklist normalize: quoted path");

    std::vector<std::wstring> apps = vn_ime::ParseProcessListText(
        L"WindowsTerminal.exe\r\n"
        L" C:\\Path\\Notepad++.EXE \r\n"
        L"Code.exe\n"
        L"notepad++.exe\r\n"
    );

    assert_true(apps.size() == 3, "Blocklist parser deduplicates normalized names");
    assert_eq(vn_ime::ProcessListToText(apps), L"windowsterminal.exe\r\nnotepad++.exe\r\ncode.exe", "Blocklist text roundtrip");
}

void test_shorthand_config_helpers() {
    std::cout << "\nRunning test_shorthand_config_helpers..." << std::endl;

    vn_ime::ShorthandParseResult parsed = vn_ime::ParseShorthandRules(
        L"# shared shorthand table\r\n"
        L"vn = Việt Nam\r\n"
        L"; another comment\r\n"
        L" KO = không \r\n"
        L"bad line\r\n"
        L"empty=\r\n"
        L"vn=VN override\r\n"
    );

    assert_true(parsed.rules.size() == 2, "Shorthand parser keeps valid unique rules");
    assert_true(parsed.invalid_lines == 2, "Shorthand parser counts invalid lines");
    assert_true(parsed.duplicate_lines == 1, "Shorthand parser counts duplicate keys");
    assert_eq(parsed.rules[0].key, L"vn", "Shorthand parser normalizes key");
    assert_eq(parsed.rules[0].value, L"VN override", "Shorthand parser last duplicate wins");
    assert_eq(parsed.rules[1].key, L"ko", "Shorthand parser lowercases ASCII keys");
    assert_eq(parsed.rules[1].value, L"không", "Shorthand parser trims value");
}

void test_engine_secure_clear() {
    std::cout << "\nRunning test_engine_secure_clear..." << std::endl;

    Engine engine(InputMethod::Telex);
    type_string(engine, L"vietes");
    assert_true(!engine.GetRawString().empty(), "Engine has raw buffer before secure clear");
    engine.SecureClear();
    assert_eq(engine.GetRawString(), L"", "SecureClear empties raw buffer");
    assert_eq(engine.GetDisplayString(), L"", "SecureClear empties display buffer");

    type_string(engine, L"hoangf");
    engine.Clear();
    assert_eq(engine.GetRawString(), L"", "Clear also empties raw buffer");
    assert_eq(engine.GetDisplayString(), L"", "Clear also empties display buffer");
}

void test_stress_and_latency() {
    std::cout << "\nRunning test_stress_and_latency (Phase 11)..." << std::endl;
    Engine engine(InputMethod::Telex);
    
    // A long text segment representing typical complex Vietnamese typing
    std::wstring text = L"dduowngf cachs mangj giair phongso danj toocj thanhf cong ddem lai j ddoocj laapj tuw do hanhj phucs cho ddongf baoof caar nuocws";
    
    // We will type this text 1000 times to stress-test the engine (total 100,000+ keystrokes)
    constexpr int iterations = 1000;
    size_t total_keystrokes = text.length() * iterations;
    
    LARGE_INTEGER frequency;
    LARGE_INTEGER start;
    LARGE_INTEGER end;
    
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);
    
    for (int i = 0; i < iterations; ++i) {
        for (wchar_t c : text) {
            if (c == L' ') {
                engine.Clear(); // Simulate committing at space
            } else {
                engine.ProcessKey(c);
            }
        }
    }
    
    QueryPerformanceCounter(&end);
    
    double elapsed_ms = static_cast<double>(end.QuadPart - start.QuadPart) * 1000.0 / frequency.QuadPart;
    double avg_us_per_key = (elapsed_ms * 1000.0) / total_keystrokes;
    
    std::cout << "  [INFO] Total Keystrokes: " << total_keystrokes << std::endl;
    std::cout << "  [INFO] Total Time: " << elapsed_ms << " ms" << std::endl;
    std::cout << "  [INFO] Avg Latency per Keystroke: " << avg_us_per_key << " microseconds" << std::endl;
    
    // Verify that average latency is less than 1.0 ms (1000 microseconds)
    assert_true(avg_us_per_key < 1000.0, "Average latency per key is under 1.0 ms");
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
    test_reconversion_helpers();
    test_golden_corpus();
    test_reconversion_ad_hoc_corpus();
    test_reconstruct_roundtrip_corpus();
    test_app_blocklist_config_helpers();
    test_shorthand_config_helpers();
    test_engine_secure_clear();
    test_stress_and_latency();

    std::cout << "\n========================================" << std::endl;
    std::cout << " TESTS SUMMARY: " << std::endl;
    std::cout << "   PASSED: " << g_tests_passed << std::endl;
    std::cout << "   FAILED: " << g_tests_failed << std::endl;
    std::cout << "========================================" << std::endl;

    return g_tests_failed > 0 ? 1 : 0;
}
