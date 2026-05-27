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

    engine.Clear();
    type_string(engine, L"huuw");
    assert_eq(engine.GetDisplayString(), L"h\u01B0u", "huuw -> huu with first-u horn");

    engine.Clear();
    type_string(engine, L"buouw");
    assert_eq(engine.GetDisplayString(), L"b\u01B0\u01A1u", "buouw -> buou with horn pair");

    engine.Clear();
    type_string(engine, L"quowr");
    assert_eq(engine.GetDisplayString(), L"qu\u1EDF", "quowr -> quo with horn, qu glide unchanged");

    engine.Clear();
    type_string(engine, L"quawn");
    assert_eq(engine.GetDisplayString(), L"qu\u0103n", "quawn -> quan with early breve");

    engine.Clear();
    type_string(engine, L"queen");
    assert_eq(engine.GetDisplayString(), L"qu\u00EAn", "queen -> quen with circumflex");

    // Free-position modifier coverage: shape key before later vowels or codas.
    engine.Clear();
    type_string(engine, L"tuwngf");
    assert_eq(engine.GetDisplayString(), L"t\u1EEBng", "tuwngf -> tung with early horn");

    engine.Clear();
    type_string(engine, L"chawn");
    assert_eq(engine.GetDisplayString(), L"ch\u0103n", "chawn -> chan with early breve");

    engine.Clear();
    type_string(engine, L"thuwa");
    assert_eq(engine.GetDisplayString(), L"th\u01B0a", "thuwa -> thua with early horn");

    engine.Clear();
    type_string(engine, L"cuwoif");
    assert_eq(engine.GetDisplayString(), L"c\u01B0\u1EDDi", "cuwoif -> cuoi with early horn");

    engine.Clear();
    type_string(engine, L"giuwa");
    assert_eq(engine.GetDisplayString(), L"gi\u01B0a", "giuwa -> giua preview with early horn");

    engine.Clear();
    type_string(engine, L"gieeu");
    assert_eq(engine.GetDisplayString(), L"gi\u00EAu", "gieeu -> gieu keeps i in the vowel group");

    engine.Clear();
    type_string(engine, L"giuwax");
    assert_eq(engine.GetDisplayString(), L"gi\u1EEFa", "giuwax -> giua with early horn");

    engine.Clear();
    type_string(engine, L"buowu");
    assert_eq(engine.GetDisplayString(), L"b\u01B0\u01A1u", "buowu -> buou with embedded horn");

    engine.Clear();
    type_string(engine, L"nguwoif");
    assert_eq(engine.GetDisplayString(), L"ng\u01B0\u1EDDi", "nguwoif -> nguoi with early horn");

    // Post-rhyme modifier coverage: shape key after the vowel group or coda.
    engine.Clear();
    type_string(engine, L"tungwf");
    assert_eq(engine.GetDisplayString(), L"t\u1EEBng", "tungwf -> tung with horn after coda");

    engine.Clear();
    type_string(engine, L"chanw");
    assert_eq(engine.GetDisplayString(), L"ch\u0103n", "chanw -> chan with breve after coda");

    engine.Clear();
    type_string(engine, L"thuaw");
    assert_eq(engine.GetDisplayString(), L"th\u01B0a", "thuaw -> thua with horn after vowel group");

    engine.Clear();
    type_string(engine, L"cuoiwf");
    assert_eq(engine.GetDisplayString(), L"c\u01B0\u1EDDi", "cuoiwf -> cuoi with horn after vowel group");

    engine.Clear();
    type_string(engine, L"giuawx");
    assert_eq(engine.GetDisplayString(), L"gi\u1EEFa", "giuawx -> giua with horn after vowel group");

    engine.Clear();
    type_string(engine, L"buouw");
    assert_eq(engine.GetDisplayString(), L"b\u01B0\u01A1u", "buouw -> buou with horn after vowel group");

    engine.Clear();
    type_string(engine, L"nguoiwf");
    assert_eq(engine.GetDisplayString(), L"ng\u01B0\u1EDDi", "nguoiwf -> nguoi with horn after vowel group");

    engine.Clear();
    type_string(engine, L"tuyeen");
    assert_eq(engine.GetDisplayString(), L"tuy\u00EAn", "tuyeen -> tuyen with circumflex after vowel group");

    // Free-position tone coverage: tone before later letters or before shape.
    engine.Clear();
    type_string(engine, L"tufngw");
    assert_eq(engine.GetDisplayString(), L"t\u1EEBng", "tufngw -> tung with early tone and late horn");

    engine.Clear();
    type_string(engine, L"hofang");
    assert_eq(engine.GetDisplayString(), L"ho\u00E0ng", "hofang -> hoang with early tone");

    engine.Clear();
    type_string(engine, L"gixuaw");
    assert_eq(engine.GetDisplayString(), L"gi\u1EEFa", "gixuaw -> giua with early tone and late horn");

    engine.Clear();
    type_string(engine, L"ngufoiw");
    assert_eq(engine.GetDisplayString(), L"ng\u01B0\u1EDDi", "ngufoiw -> nguoi with early tone and late horn");

    // Free-style modifications
    engine.Clear();
    type_string(engine, L"vietje");
    assert_eq(engine.GetDisplayString(), L"việt", "vietje -> việt");

    engine.Clear();
    type_string(engine, L"vietes");
    assert_eq(engine.GetDisplayString(), L"vi\u1EBFt", "vietes -> viet acute");

    engine.Clear();
    type_string(engine, L"kieemr");
    assert_eq(engine.GetDisplayString(), L"ki\u1EC3m", "kieemr -> kiem hook");

    engine.Clear();
    type_string(engine, L"kireem");
    assert_eq(engine.GetDisplayString(), L"ki\u1EC3m", "kireem -> kiem hook with early tone");

    engine.Clear();
    type_string(engine, L"Kireem");
    assert_eq(engine.GetDisplayString(), L"Ki\u1EC3m", "Kireem -> Kiem hook with early tone");

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

    engine.Clear();
    type_string(engine, L"huu7");
    assert_eq(engine.GetDisplayString(), L"h\u01B0u", "huu7 -> huu with first-u horn");

    engine.Clear();
    type_string(engine, L"buou7");
    assert_eq(engine.GetDisplayString(), L"b\u01B0\u01A1u", "buou7 -> buou with horn pair");

    engine.Clear();
    type_string(engine, L"muoi72");
    assert_eq(engine.GetDisplayString(), L"m\u01B0\u1EDDi", "muoi72 -> muoi with horn pair and grave");

    engine.Clear();
    type_string(engine, L"quo73");
    assert_eq(engine.GetDisplayString(), L"qu\u1EDF", "quo73 -> quo with horn, qu glide unchanged");

    engine.Clear();
    type_string(engine, L"qua8n");
    assert_eq(engine.GetDisplayString(), L"qu\u0103n", "qua8n -> quan with early breve");

    engine.Clear();
    type_string(engine, L"que6n");
    assert_eq(engine.GetDisplayString(), L"qu\u00EAn", "que6n -> quen with circumflex");

    // Free-position modifier coverage: shape digit before later vowels or codas.
    engine.Clear();
    type_string(engine, L"tu7ng2");
    assert_eq(engine.GetDisplayString(), L"t\u1EEBng", "tu7ng2 -> tung with early horn");

    engine.Clear();
    type_string(engine, L"cha8n");
    assert_eq(engine.GetDisplayString(), L"ch\u0103n", "cha8n -> chan with early breve");

    engine.Clear();
    type_string(engine, L"thu7a");
    assert_eq(engine.GetDisplayString(), L"th\u01B0a", "thu7a -> thua with early horn");

    engine.Clear();
    type_string(engine, L"cu7oi2");
    assert_eq(engine.GetDisplayString(), L"c\u01B0\u1EDDi", "cu7oi2 -> cuoi with early horn");

    engine.Clear();
    type_string(engine, L"giu7a");
    assert_eq(engine.GetDisplayString(), L"gi\u01B0a", "giu7a -> giua preview with early horn");

    engine.Clear();
    type_string(engine, L"gieu6");
    assert_eq(engine.GetDisplayString(), L"gi\u00EAu", "gieu6 -> gieu keeps i in the vowel group");

    engine.Clear();
    type_string(engine, L"giu7a4");
    assert_eq(engine.GetDisplayString(), L"gi\u1EEFa", "giu7a4 -> giua with early horn");

    engine.Clear();
    type_string(engine, L"buo7u");
    assert_eq(engine.GetDisplayString(), L"b\u01B0\u01A1u", "buo7u -> buou with embedded horn");

    engine.Clear();
    type_string(engine, L"ngu7oi2");
    assert_eq(engine.GetDisplayString(), L"ng\u01B0\u1EDDi", "ngu7oi2 -> nguoi with early horn");

    engine.Clear();
    type_string(engine, L"tuye6n1");
    assert_eq(engine.GetDisplayString(), L"tuy\u1EBFn", "tuye6n1 -> tuyen with early circumflex");

    // Post-rhyme modifier coverage: shape digit after the vowel group or coda.
    engine.Clear();
    type_string(engine, L"tung72");
    assert_eq(engine.GetDisplayString(), L"t\u1EEBng", "tung72 -> tung with horn after coda");

    engine.Clear();
    type_string(engine, L"chan8");
    assert_eq(engine.GetDisplayString(), L"ch\u0103n", "chan8 -> chan with breve after coda");

    engine.Clear();
    type_string(engine, L"thua7");
    assert_eq(engine.GetDisplayString(), L"th\u01B0a", "thua7 -> thua with horn after vowel group");

    engine.Clear();
    type_string(engine, L"cuoi72");
    assert_eq(engine.GetDisplayString(), L"c\u01B0\u1EDDi", "cuoi72 -> cuoi with horn after vowel group");

    engine.Clear();
    type_string(engine, L"giua74");
    assert_eq(engine.GetDisplayString(), L"gi\u1EEFa", "giua74 -> giua with horn after vowel group");

    engine.Clear();
    type_string(engine, L"buou7");
    assert_eq(engine.GetDisplayString(), L"b\u01B0\u01A1u", "buou7 -> buou with horn after vowel group");

    engine.Clear();
    type_string(engine, L"nguoi72");
    assert_eq(engine.GetDisplayString(), L"ng\u01B0\u1EDDi", "nguoi72 -> nguoi with horn after vowel group");

    engine.Clear();
    type_string(engine, L"tuyen61");
    assert_eq(engine.GetDisplayString(), L"tuy\u1EBFn", "tuyen61 -> tuyen with circumflex after vowel group");

    // Free-position tone coverage: tone before later letters or before shape.
    engine.Clear();
    type_string(engine, L"tu2ng7");
    assert_eq(engine.GetDisplayString(), L"t\u1EEBng", "tu2ng7 -> tung with early tone and late horn");

    engine.Clear();
    type_string(engine, L"ho2ang");
    assert_eq(engine.GetDisplayString(), L"ho\u00E0ng", "ho2ang -> hoang with early tone");

    engine.Clear();
    type_string(engine, L"gi4ua7");
    assert_eq(engine.GetDisplayString(), L"gi\u1EEFa", "gi4ua7 -> giua with early tone and late horn");

    engine.Clear();
    type_string(engine, L"ngu2oi7");
    assert_eq(engine.GetDisplayString(), L"ng\u01B0\u1EDDi", "ngu2oi7 -> nguoi with early tone and late horn");

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

    // Free-position tone improvements: tone before vowel group
    // tr2o -> trò
    engine.Clear();
    type_string(engine, L"tr2o");
    assert_eq(engine.GetDisplayString(), L"trò", "tr2o -> trò");

    // ch1o -> chó
    engine.Clear();
    type_string(engine, L"ch1o");
    assert_eq(engine.GetDisplayString(), L"chó", "ch1o -> chó");

    // tr1 -> tr1, then tr1o -> tró
    engine.Clear();
    type_string(engine, L"tr1");
    assert_eq(engine.GetDisplayString(), L"tr1", "tr1 -> tr1 (no vowel, literal)");
    type_string(engine, L"o");
    assert_eq(engine.GetDisplayString(), L"tró", "tr1o -> tró (vowel typed after tone)");

    // 123 -> 123
    engine.Clear();
    type_string(engine, L"123");
    assert_eq(engine.GetDisplayString(), L"123", "123 -> 123");

    // 2a -> 2a
    engine.Clear();
    type_string(engine, L"2a");
    assert_eq(engine.GetDisplayString(), L"2a", "2a -> 2a");
}

void test_backspace_undo() {
    std::cout << "\nRunning test_backspace_undo..." << std::endl;
    Engine engine(InputMethod::Telex);

    // Raw backspace keeps the old engine behavior for low-level callers.
    engine.Clear();
    type_string(engine, L"hoangs");
    assert_eq(engine.GetDisplayString(), L"hoáng", "Pre-backspace: hoáng");
    
    engine.Backspace();
    assert_eq(engine.GetDisplayString(), L"hoang", "Backspace once -> hoang");

    engine.Backspace();
    assert_eq(engine.GetDisplayString(), L"hoan", "Backspace twice -> hoan");

    // IME backspace deletes the displayed character, including its accent.
    engine.Clear();
    type_string(engine, L"hoangs");
    assert_eq(engine.GetDisplayString(), L"hoáng", "Pre-display-backspace: hoáng");

    engine.BackspaceDisplayChar();
    assert_eq(engine.GetDisplayString(), L"hoán", "Display backspace once -> hoán");

    engine.BackspaceDisplayChar();
    assert_eq(engine.GetDisplayString(), L"hóa", "Display backspace twice -> hóa");

    engine.Clear();
    type_string(engine, L"as");
    assert_eq(engine.GetDisplayString(), L"á", "Pre-display-backspace: á");

    engine.BackspaceDisplayChar();
    assert_eq(engine.GetDisplayString(), L"", "Display backspace removes accented single char");
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

    // Test overrides for uo -> uô/ươ prioritization conflicts
    engine.Clear();
    type_string(engine, L"muons");
    assert_eq(engine.GetDisplayString(), L"muốn", "muons -> muốn");

    engine.Clear();
    type_string(engine, L"cuocj");
    assert_eq(engine.GetDisplayString(), L"cuộc", "cuocj -> cuộc");

    engine.Clear();
    type_string(engine, L"luonf");
    assert_eq(engine.GetDisplayString(), L"luồn", "luonf -> luồn");

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

    // 6. Phonotactic spelling bypass for invalid combinations
    engine.Clear();
    type_string(engine, L"anhw");
    assert_eq(engine.GetDisplayString(), L"anhw", "anhw -> anhw (bypass invalid ănh)");

    Engine engine_vni(InputMethod::VNI);
    engine_vni.Clear();
    type_string(engine_vni, L"anh8");
    assert_eq(engine_vni.GetDisplayString(), L"anh8", "anh8 -> anh8 (bypass invalid ănh)");

    // 7. Informal slang retention
    engine.Clear();
    type_string(engine, L"cumr");
    assert_eq(engine.GetDisplayString(), L"củm", "cumr -> củm (informal slang retained)");

    engine_vni.Clear();
    type_string(engine_vni, L"cum3");
    assert_eq(engine_vni.GetDisplayString(), L"củm", "cum3 -> củm (informal slang retained)");

    // Test progression of in-progress word containing tone keys
    // e.g. tiees (Telex) -> tiết, but tieesp -> tiếp
    engine.Clear();
    type_string(engine, L"tiees");
    assert_eq(engine.GetDisplayString(), L"tiết", "tiees -> tiết (missing t typo correction)");
    type_string(engine, L"p");
    assert_eq(engine.GetDisplayString(), L"tiếp", "tieesp -> tiếp (progression works)");

    // tie61 (VNI) -> tiết, but tie61p -> tiếp
    engine_vni.Clear();
    type_string(engine_vni, L"tie61");
    assert_eq(engine_vni.GetDisplayString(), L"tiết", "tie61 -> tiết (missing t typo correction)");
    type_string(engine_vni, L"p");
    assert_eq(engine_vni.GetDisplayString(), L"tiếp", "tie61p -> tiếp (progression works)");

    // muon1 (VNI) -> muốn
    engine_vni.Clear();
    type_string(engine_vni, L"muon1");
    assert_eq(engine_vni.GetDisplayString(), L"muốn", "muon1 -> muốn");
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

    auto assert_span = [](std::wstring_view text, size_t sel_start, size_t sel_end,
                          size_t expected_start, size_t expected_end,
                          const std::string& name) {
        auto span = rules::ResolveReconversionSpan(text, sel_start, sel_end);
        assert_true(span.has_value(), name + " resolves");
        if (span) {
            assert_true(span->start == expected_start && span->end == expected_end, name + " selects whole word");
        }
    };

    assert_span(L"duong", 2, 2, 0, 5, "Caret inside duong");
    assert_span(L"duong", 0, 0, 0, 5, "Caret at start of duong");
    assert_span(L"duong", 5, 5, 0, 5, "Caret at end of duong");
    assert_span(L"nguoi", 3, 3, 0, 5, "Caret inside nguoi");
    assert_span(L"hoang", 2, 2, 0, 5, "Caret inside hoang");
    assert_span(L"giua", 2, 2, 0, 4, "Caret inside giua");
    assert_span(L"duong", 1, 4, 0, 5, "Selection within word");

    assert_true(!rules::ResolveReconversionSpan(L"hoang  ", 7, 7).has_value(), "No reconversion after spaces");
    assert_true(!rules::ResolveReconversionSpan(L"hoang\t", 6, 6).has_value(), "No reconversion after tab");
    assert_true(!rules::ResolveReconversionSpan(L"thị ", 4, 4).has_value(), "VNI digit after spaced toned word stays literal");
    assert_true(!rules::ResolveReconversionSpan(L"hoang.", 6, 6).has_value(), "No reconversion after punctuation");
    assert_true(!rules::ResolveReconversionSpan(L"hoang\n", 6, 6).has_value(), "No reconversion after newline");
    assert_true(!rules::ResolveReconversionSpan(L"duong dep", 1, 7).has_value(), "No multi-word reconversion selection");
    assert_true(!rules::ResolveReconversionSpan(L"duong", 2, 2, true, false).has_value(), "Reject left-truncated token");
    assert_true(!rules::ResolveReconversionSpan(L"duong", 2, 2, false, true).has_value(), "Reject right-truncated token");
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

    auto assert_candidate = [](std::wstring_view committed_word, wchar_t key, InputMethod method,
                               const std::wstring& expected, const std::string& test_name) {
        auto candidate = BuildReconversionCandidate(committed_word, key, method);
        assert_true(candidate.has_value(), test_name + " has candidate");
        if (candidate) {
            assert_eq(*candidate, expected, test_name);
        }
    };

    assert_candidate(L"hoang", L's', InputMethod::Telex, L"ho\u00E1ng", "Ad-hoc reconversion: hoang + s");
    assert_candidate(L"hoang", L'f', InputMethod::Telex, L"ho\u00E0ng", "Ad-hoc reconversion: hoang + f");
    assert_candidate(L"ho\u00E0ng", L's', InputMethod::Telex, L"ho\u00E1ng", "Ad-hoc reconversion: hoang grave + s");
    assert_candidate(L"duong", L'w', InputMethod::Telex, L"d\u01B0\u01A1ng", "Ad-hoc reconversion: duong + w");
    assert_candidate(L"hoang", L'1', InputMethod::VNI, L"ho\u00E1ng", "Ad-hoc reconversion VNI: hoang + 1");
    assert_candidate(L"nguoi", L'7', InputMethod::VNI, L"ng\u01B0\u01A1i", "Full-token reconversion VNI: nguoi + 7");
    assert_candidate(L"giua", L'7', InputMethod::VNI, L"gi\u01B0a", "Full-token reconversion VNI: giua + 7");
    assert_candidate(L"quo", L'7', InputMethod::VNI, L"qu\u01A1", "Full-token reconversion VNI: quo + 7");
    assert_candidate(L"hư", L'u', InputMethod::Telex, L"hưu", "Ad-hoc reconversion: hư + u");
    assert_candidate(L"hưu", L'x', InputMethod::Telex, L"hữu", "Ad-hoc reconversion: hưu + x");

    assert_true(!BuildReconversionCandidate(L"github", L's', InputMethod::Telex).has_value(),
                "Invalid English reconversion is rejected");

    auto rename_edit = BuildReconversionEdit(L"duong.txt", 2, 2, L'w', InputMethod::Telex);
    assert_true(rename_edit.has_value(), "Win32 edit reconversion resolves filename token");
    if (rename_edit) {
        assert_true(rename_edit->start == 0 && rename_edit->end == 5,
                    "Win32 edit reconversion replaces only filename stem");
        assert_true(rename_edit->selection_start == 2 && rename_edit->selection_end == 2,
                    "Win32 edit reconversion preserves caret offset");
        assert_eq(rename_edit->replacement, L"d\u01B0\u01A1ng", "Win32 edit reconversion replacement");
    }

    auto selected_edit = BuildReconversionEdit(L"xx hoang yy", 3, 7, L'f', InputMethod::Telex);
    assert_true(selected_edit.has_value(), "Win32 edit reconversion expands selection inside token");
    if (selected_edit) {
        assert_true(selected_edit->start == 3 && selected_edit->end == 8,
                    "Win32 edit reconversion selection target bounds");
        assert_eq(selected_edit->replacement, L"ho\u00E0ng", "Win32 edit selected token replacement");
    }

    assert_true(!BuildReconversionEdit(L"duong dep", 1, 7, L'w', InputMethod::Telex).has_value(),
                "Win32 edit reconversion rejects multi-word selection");
    assert_true(!BuildReconversionEdit(L"duong", 2, 2, L'w', InputMethod::Telex, true, false).has_value(),
                "Win32 edit reconversion rejects left-truncated token");
}

void test_excel_formula_context() {
    std::cout << "\nRunning test_excel_formula_context..." << std::endl;

    assert_true(ClassifyExcelFormulaPrefix(L"=std") == ExcelFormulaInputKind::FormulaSyntax,
                "Excel formula function token is native syntax");
    assert_true(ClassifyExcelFormulaPrefix(L"=IF(A1,ST") == ExcelFormulaInputKind::FormulaSyntax,
                "Excel nested function token is native syntax");
    assert_true(ClassifyExcelFormulaPrefix(L"=\"kiemr") == ExcelFormulaInputKind::QuotedText,
                "Excel formula string permits Vietnamese composition");
    assert_true(ClassifyExcelFormulaPrefix(L"=IF(A1,\"kiemr") == ExcelFormulaInputKind::QuotedText,
                "Excel function string permits Vietnamese composition");
    assert_true(ClassifyExcelFormulaPrefix(L"=\"a\"\"kiemr") == ExcelFormulaInputKind::QuotedText,
                "Excel escaped quote remains inside string");
    assert_true(ClassifyExcelFormulaPrefix(L"=\"a\"\"\"") == ExcelFormulaInputKind::FormulaSyntax,
                "Excel closing quote returns to formula syntax");
    assert_true(ClassifyExcelFormulaPrefix(L"kiemr") == ExcelFormulaInputKind::NotFormula,
                "Excel regular cell input is not formula syntax");
    assert_true(ClassifyExcelFormulaPrefix(L"=std", true) == ExcelFormulaInputKind::Unknown,
                "Excel truncated prefix is unknown");

    ExcelFormulaSessionState state = ExcelFormulaSessionState::Idle;
    state = AdvanceExcelFormulaSessionState(state, L'=');
    assert_true(state == ExcelFormulaSessionState::PendingFormulaStart,
                "Excel equals arms pending formula start");
    assert_true(MergeExcelFormulaSessionProbe(state, ExcelFormulaInputKind::Unknown) ==
                    ExcelFormulaSessionState::PendingFormulaStart,
                "Excel unknown TSF probe does not drop pending keyed state");
    state = AdoptPendingExcelFormulaSession(state);
    assert_true(state == ExcelFormulaSessionState::FormulaSyntax,
                "Excel pending formula adopts inline editor context");
    assert_true(AdoptPendingExcelFormulaSession(state) == ExcelFormulaSessionState::FormulaSyntax,
                "Excel formula context handoff is one-shot");
    state = AdvanceExcelFormulaSessionState(state, L's');
    assert_true(state == ExcelFormulaSessionState::FormulaSyntax,
                "Excel formula letters stay native syntax");
    state = AdvanceExcelFormulaSessionState(state, L'"');
    assert_true(state == ExcelFormulaSessionState::QuotedText,
                "Excel opening quote enables Vietnamese quoted text");
    state = AdvanceExcelFormulaSessionState(state, L'"');
    state = AdvanceExcelFormulaSessionState(state, L'"');
    assert_true(state == ExcelFormulaSessionState::QuotedText,
                "Excel escaped quote pair stays inside quoted text");
    state = AdvanceExcelFormulaSessionState(state, L'"');
    assert_true(state == ExcelFormulaSessionState::FormulaSyntax,
                "Excel closing quote returns to formula syntax mode");
    assert_true(MergeExcelFormulaSessionProbe(state, ExcelFormulaInputKind::Unknown) ==
                    ExcelFormulaSessionState::FormulaSyntax,
                "Excel unknown TSF probe does not drop keyed formula state");
    state = AdvanceExcelFormulaSessionState(state, 0, true);
    assert_true(state == ExcelFormulaSessionState::Idle,
                "Excel reset event clears formula state");
    state = AdvanceExcelFormulaSessionState(ExcelFormulaSessionState::Idle, L'=');
    state = AdvanceExcelFormulaSessionState(state, 0, true);
    assert_true(state == ExcelFormulaSessionState::Idle,
                "Excel invalidating event clears pending handoff");
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

    vn_ime::IMEConfig defaults;
    assert_true(defaults.enable_app_blocklist, "Blocklist defaults to enabled for terminal native input");
    assert_true(defaults.blocked_apps.empty(), "Blocked apps list is empty by default to support terminal apps");

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

void test_reconversion_span_latency() {
    std::cout << "\nRunning test_reconversion_span_latency..." << std::endl;
    constexpr int iterations = 100000;
    volatile size_t observed = 0;
    LARGE_INTEGER frequency;
    LARGE_INTEGER start;
    LARGE_INTEGER end;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);
    for (int i = 0; i < iterations; ++i) {
        auto span = rules::ResolveReconversionSpan(L"mot duong dang viet", 7, 7);
        if (span) observed += span->end - span->start;
    }
    QueryPerformanceCounter(&end);
    const double elapsed_us = static_cast<double>(end.QuadPart - start.QuadPart) * 1000000.0 / frequency.QuadPart;
    const double average_us = elapsed_us / iterations;
    std::cout << "  [INFO] Average reconversion span resolve: " << average_us << " microseconds" << std::endl;
    assert_true(observed != 0 && average_us < 1000.0, "Reconversion span resolution is under 1.0 ms");
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
    test_excel_formula_context();
    test_reconstruct_roundtrip_corpus();
    test_app_blocklist_config_helpers();
    test_shorthand_config_helpers();
    test_engine_secure_clear();
    test_stress_and_latency();
    test_reconversion_span_latency();

    std::cout << "\n========================================" << std::endl;
    std::cout << " TESTS SUMMARY: " << std::endl;
    std::cout << "   PASSED: " << g_tests_passed << std::endl;
    std::cout << "   FAILED: " << g_tests_failed << std::endl;
    std::cout << "========================================" << std::endl;

    return g_tests_failed > 0 ? 1 : 0;
}
