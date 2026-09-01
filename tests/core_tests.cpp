#include <iostream>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <cassert>
#include <chrono>
#include <utility>
#include <vector>
#include <windows.h>
#include <msctf.h>
#include "engine.hpp"
#include "rules.hpp"
#include "speller.hpp"
#include "speller_data.hpp"
#include "english_lexicon_generated.hpp"
#include "config.hpp"
#include "shorthand_reload.hpp"
#include "shorthand_template.hpp"
#include "commit_undo.hpp"
#include "commit_transform.hpp"
#include "dialog_layout.hpp"
#include "browser_interaction.hpp"
#include "hotkey_toggle_state.hpp"
#include "tray_click_state.hpp"
#include "word_inline_policy.hpp"
#include "key_translation.hpp"
#include "fake_backspace_handler.hpp"
#include "password_context_policy.hpp"

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

void assert_true(bool condition, const std::string& test_name);

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

    // A lone w should stay literal; use uw/uow when the user really wants ư/ươ.
    engine.Clear();
    type_string(engine, L"w");
    assert_eq(engine.GetDisplayString(), L"w", "single w stays literal");

    // uw -> ư
    engine.Clear();
    type_string(engine, L"uw");
    assert_eq(engine.GetDisplayString(), L"ư", "uw -> ư");

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

    engine.Clear();
    type_string(engine, L"thuowr");
    assert_eq(engine.GetDisplayString(), L"thu\u1EDF", "thuowr -> thuở, not thửơ");

    engine.Clear();
    type_string(engine, L"thuwor");
    assert_eq(engine.GetDisplayString(), L"thu\u1EDF", "thuwor -> thuở, not thửơ");

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

    // VNI double modification escape sequence tests
    // u77 -> u7
    engine.Clear();
    type_string(engine, L"u77");
    assert_eq(engine.GetDisplayString(), L"u7", "u77 -> u7");

    // a88 -> a8
    engine.Clear();
    type_string(engine, L"a88");
    assert_eq(engine.GetDisplayString(), L"a8", "a88 -> a8");

    // a66 -> a6
    engine.Clear();
    type_string(engine, L"a66");
    assert_eq(engine.GetDisplayString(), L"a6", "a66 -> a6");

    // d99 -> d9
    engine.Clear();
    type_string(engine, L"d99");
    assert_eq(engine.GetDisplayString(), L"d9", "d99 -> d9");

    // u777 -> ư7
    engine.Clear();
    type_string(engine, L"u777");
    assert_eq(engine.GetDisplayString(), L"ư7", "u777 -> ư7");

    // a888 -> ă8
    engine.Clear();
    type_string(engine, L"a888");
    assert_eq(engine.GetDisplayString(), L"ă8", "a888 -> ă8");

    // d999 -> đ9
    engine.Clear();
    type_string(engine, L"d999");
    assert_eq(engine.GetDisplayString(), L"đ9", "d999 -> đ9");

    // e110 -> e10
    engine.Clear();
    type_string(engine, L"e110");
    assert_eq(engine.GetDisplayString(), L"e10", "e110 -> e10");

    // e1107 -> e107
    engine.Clear();
    type_string(engine, L"e1107");
    assert_eq(engine.GetDisplayString(), L"e107", "e1107 -> e107");

    // u770 -> u70
    engine.Clear();
    type_string(engine, L"u770");
    assert_eq(engine.GetDisplayString(), L"u70", "u770 -> u70");
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
        if (!(vn_ime::core::speller::DICTIONARY[i-1] <
              vn_ime::core::speller::DICTIONARY[i])) {
            std::cout << "Dictionary NOT sorted at index " << i << ": " 
                      << to_utf8(std::wstring(vn_ime::core::speller::DICTIONARY[i-1])) << " > " 
                      << to_utf8(std::wstring(vn_ime::core::speller::DICTIONARY[i])) << std::endl;
            is_sorted = false;
            break;
        }
    }
    if (is_sorted) {
        std::cout << "  [PASS] DICTIONARY is sorted and unique" << std::endl;
        g_tests_passed++;
    } else {
        std::cout << "  [FAIL] DICTIONARY is NOT sorted and unique!" << std::endl;
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

    assert_true(speller::IsInDictionary(L"alo") &&
                    speller::IsInDictionary(L"lao"),
                "Vietnamese dictionary contains both alo and lao");

    bool alo_preserved = true;
    for (const InputMethod method : {
             InputMethod::Telex,
             InputMethod::SimpleTelex,
             InputMethod::VNI}) {
        for (const CorrectionLevel correction : {
                 CorrectionLevel::Normal,
                 CorrectionLevel::Advanced,
                 CorrectionLevel::Experimental}) {
            for (const EnglishProtectionLevel bilingual_level : {
                     EnglishProtectionLevel::Off,
                     EnglishProtectionLevel::Balanced,
                     EnglishProtectionLevel::EnglishFirst}) {
                for (const std::wstring_view input : {L"alo", L"Alo"}) {
                    Engine alo_engine(method);
                    alo_engine.SetCorrectionLevel(correction);
                    alo_engine.SetEnglishProtectionLevel(bilingual_level);
                    type_string(alo_engine, input);
                    if (alo_engine.GetDisplayString() != input) {
                        alo_preserved = false;
                    }
                }
            }
        }
    }
    assert_true(alo_preserved,
                "alo remains Vietnamese in every method, correction and bilingual mode");

    // 1. Tone shifting / correction: hòa -> hoà
    engine.Clear();
    type_string(engine, L"hoaf"); // default engine output will be "hòa" (no final consonant, tone on o)
    assert_eq(engine.GetDisplayString(), L"hoà", "hoaf -> hoà (tone shift to matching dictionary syllable)");

    // 2. Typo correction: thuyes -> thuyết
    engine.Clear();
    type_string(engine, L"thuyes");
    assert_eq(engine.GetDisplayString(), L"thuyết", "thuyes -> thuyết (missing t correction)");

    engine.Clear();
    type_string(engine, L"vies");
    assert_eq(engine.GetDisplayString(), L"viết", "vies -> viết (missing t correction)");

    engine.Clear();
    type_string(engine, L"khuyes");
    assert_eq(engine.GetDisplayString(), L"khuyết", "khuyes -> khuyết (generalized suffix missing t correction)");

    engine.Clear();
    type_string(engine, L"tuyes");
    assert_eq(engine.GetDisplayString(), L"tuyết", "tuyes -> tuyết (generalized suffix missing t correction)");

    Engine engine_vni(InputMethod::VNI);

    // 3. Typo correction whitelist depends on input method but shares the same target family.
    engine.Clear();
    type_string(engine, L"tuyetn");
    assert_eq(engine.GetDisplayString(), L"tuyền", "Telex Normal: tuyetn -> tuyền via tone-key adjacency");

    engine.Clear();
    type_string(engine, L"vietn");
    assert_eq(engine.GetDisplayString(), L"vi\u1EC1n", "Telex Normal: vietn -> vienf candidate");

    engine.Clear();
    type_string(engine, L"thietn");
    assert_eq(engine.GetDisplayString(), L"thi\u1EC1n", "Telex Normal: thietn -> thienf candidate");

    engine.Clear();
    type_string(engine, L"kietn");
    assert_eq(engine.GetDisplayString(), L"kietn", "Telex Normal: kietn stays raw outside whitelist");

    engine_vni.Clear();
    type_string(engine_vni, L"tuyetn");
    assert_eq(engine_vni.GetDisplayString(), L"tuy\u1EC1n", "VNI Normal: tuyetn -> tuyenf-family candidate");

    // VNI whitelist additions.
    engine_vni.Clear();
    type_string(engine_vni, L"vietn");
    assert_eq(engine_vni.GetDisplayString(), L"vi\u1EC1n", "VNI Normal: vietn -> vienf-family candidate");

    engine_vni.Clear();
    type_string(engine_vni, L"thietn");
    assert_eq(engine_vni.GetDisplayString(), L"thi\u1EC1n", "VNI Normal: thietn -> thienf-family candidate");

    engine_vni.Clear();
    type_string(engine_vni, L"kietn");
    assert_eq(engine_vni.GetDisplayString(), L"kietn", "VNI Normal: kietn stays raw outside whitelist");

    // 4. Typo correction: dduocj -> duoc vowel substitution.
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

    engine_vni.Clear();
    type_string(engine_vni, L"anh8");
    assert_eq(engine_vni.GetDisplayString(), L"anh8", "anh8 -> anh8 (bypass invalid ănh)");

    engine.Clear();
    type_string(engine, L"khoas");
    assert_eq(engine.GetDisplayString(), L"kho\u00E1", "khoas -> khoa acute without missing-t correction");

    engine_vni.Clear();
    type_string(engine_vni, L"khoa1");
    assert_eq(engine_vni.GetDisplayString(), L"kho\u00E1", "khoa1 -> khoa acute without missing-t correction");

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

    engine_vni.Clear();
    type_string(engine_vni, L"tie61");
    assert_eq(engine_vni.GetDisplayString(), L"tiết", "tie61 -> tiết (missing t typo correction)");
    type_string(engine_vni, L"p");
    assert_eq(engine_vni.GetDisplayString(), L"tiếp", "tie61p -> tiếp (progression works)");

    engine_vni.Clear();
    type_string(engine_vni, L"khuye1");
    assert_eq(engine_vni.GetDisplayString(), L"khuyết", "khuye1 -> khuyết (generalized suffix VNI missing t correction)");

    engine_vni.Clear();
    type_string(engine_vni, L"tuye1");
    assert_eq(engine_vni.GetDisplayString(), L"tuyết", "tuye1 -> tuyết (generalized suffix VNI missing t correction)");

    // muon1 (VNI) -> muốn
    engine_vni.Clear();
    type_string(engine_vni, L"muon1");
    assert_eq(engine_vni.GetDisplayString(), L"muốn", "muon1 -> muốn");

    // vnd9 (VNI) -> vnđ
    engine_vni.Clear();
    type_string(engine_vni, L"vnd9");
    assert_eq(engine_vni.GetDisplayString(), L"vnđ", "vnd9 -> vnđ");

    // qd9 (VNI) -> qđ
    engine_vni.Clear();
    type_string(engine_vni, L"qd9");
    assert_eq(engine_vni.GetDisplayString(), L"qđ", "qd9 -> qđ");

    // vndd (Telex) -> vnđ
    engine.Clear();
    type_string(engine, L"vndd");
    assert_eq(engine.GetDisplayString(), L"vnđ", "vndd -> vnđ");

    // qdd (Telex) -> qđ
    engine.Clear();
    type_string(engine, L"qdd");
    assert_eq(engine.GetDisplayString(), L"qđ", "qdd -> qđ");

    // Backspace on abbreviation: vndd -> Backspace -> vnd
    engine.Clear();
    type_string(engine, L"vndd");
    engine.Backspace();
    assert_eq(engine.GetDisplayString(), L"vnd", "vndd -> Backspace -> vnd");

    engine.Clear();
    type_string(engine, L"gius");
    assert_eq(engine.GetDisplayString(), L"gi\u00FA", "gius keeps giu acute preview, not giut");
    type_string(engine, L"p");
    assert_eq(engine.GetDisplayString(), L"gi\u00FAp", "giusp progresses to giup");

    engine_vni.Clear();
    type_string(engine_vni, L"giu1");
    assert_eq(engine_vni.GetDisplayString(), L"gi\u00FA", "giu1 keeps giu acute preview, not giut");
    type_string(engine_vni, L"p");
    assert_eq(engine_vni.GetDisplayString(), L"gi\u00FAp", "giu1p progresses to giup");

    engine_vni.Clear();
    type_string(engine_vni, L"thuo6");
    assert_eq(engine_vni.GetDisplayString(), L"thu\u00F4", "thuo6 keeps uo circumflex preview, not thuo hook autocorrect");
    type_string(engine_vni, L"c5");
    assert_eq(engine_vni.GetDisplayString(), L"thu\u1ED9c", "thuo6c5 progresses to thuoc dot");

    engine_vni.Clear();
    type_string(engine_vni, L"thuoc65");
    assert_eq(engine_vni.GetDisplayString(), L"thu\u1ED9c", "thuoc65 progresses to thuoc dot");

    engine_vni.Clear();
    type_string(engine_vni, L"thuoc6");
    assert_eq(engine_vni.GetDisplayString(), L"thu\u00F4c", "thuoc6 keeps circumflex before final tone");
    type_string(engine_vni, L"5");
    assert_eq(engine_vni.GetDisplayString(), L"thu\u1ED9c", "thuoc6 then 5 progresses to thuoc dot");

    engine_vni.Clear();
    type_string(engine_vni, L"thuo7c65");
    assert_eq(engine_vni.GetDisplayString(), L"thu\u1ED9c", "thuo7c65 overrides horn pair to circumflex and dot");

    engine_vni.Clear();
    type_string(engine_vni, L"hon7");
    assert_eq(engine_vni.GetDisplayString(), L"h\u01A1n", "hon7 -> hon horn");
    type_string(engine_vni, L"6");
    assert_eq(engine_vni.GetDisplayString(), L"h\u00F4n", "hon7 then 6 overrides horn to circumflex");

    engine_vni.Clear();
    type_string(engine_vni, L"thuo73");
    assert_eq(engine_vni.GetDisplayString(), L"thu\u1EDF", "thuo73 still supports thuo -> thuo horn hook");

    engine_vni.Clear();
    type_string(engine_vni, L"thuo37");
    assert_eq(engine_vni.GetDisplayString(), L"thu\u1EDF", "thuo37 still supports early hook then horn");
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

void test_browser_url_native_reconversion_policy() {
    std::cout << "\nRunning test_browser_url_native_reconversion_policy..." << std::endl;

    assert_true(
        vn_ime::IsBrowserExecutableName(L"opera.exe") &&
            vn_ime::IsBrowserExecutableName(L"msedge.exe") &&
            !vn_ime::IsBrowserExecutableName(L"codex.exe") &&
            vn_ime::IsWebRichTextHostExecutableName(L"codex.exe") &&
            !vn_ime::IsWebRichTextHostExecutableName(L"notepad.exe"),
        "Web rich-text host policy includes browsers and Codex without widening browser input-scope detection");
    assert_true(
        vn_ime::ShouldPassWebRichTextBoundaryToHost(
            true, true, true, false, false, false, L' ') &&
            vn_ime::ShouldPassWebRichTextBoundaryToHost(
                true, true, false, false, false, false, L'.') &&
            vn_ime::ShouldPassWebRichTextBoundaryToHost(
                true, true, false, false, false, false, L','),
        "Web rich-text Space and ordinary punctuation commit then stay host-native");
    assert_true(
        !vn_ime::ShouldPassWebRichTextBoundaryToHost(
            true, true, false, true, false, false, L'\b') &&
            !vn_ime::ShouldPassWebRichTextBoundaryToHost(
                true, true, false, false, true, false, L'a') &&
            !vn_ime::ShouldPassWebRichTextBoundaryToHost(
                true, true, false, false, false, true, L'@') &&
            !vn_ime::ShouldPassWebRichTextBoundaryToHost(
                true, true, false, false, false, true, L'.') &&
            !vn_ime::ShouldPassWebRichTextBoundaryToHost(
                false, true, true, false, false, false, L' ') &&
            !vn_ime::ShouldPassWebRichTextBoundaryToHost(
                true, false, true, false, false, false, L' '),
        "Backspace, Vietnamese keys, URL/email continuation, non-web and idle states keep existing routing");

    const InputScope url_scope[] = {IS_URL};
    const InputScope search_scope[] = {IS_SEARCH};
    const InputScope default_scope[] = {IS_DEFAULT};
    const InputScope email_scope[] = {IS_EMAIL_SMTPEMAILADDRESS};
    const InputScope password_scope[] = {IS_PASSWORD};
    const InputScope url_and_password[] = {IS_URL, IS_PASSWORD};

    assert_true(
        vn_ime::SelectBrowserTextInputMode(
            true, false, url_scope) ==
            vn_ime::BrowserTextInputMode::UrlNativeReconversion,
        "Browser IS_URL selects native typed-reconversion mode");
    assert_true(
        vn_ime::SelectBrowserTextInputMode(
            false, false, url_scope) ==
            vn_ime::BrowserTextInputMode::NativeComposition &&
        vn_ime::SelectBrowserTextInputMode(
            true, true, url_scope) ==
            vn_ime::BrowserTextInputMode::NativeComposition,
        "Non-browser and secure URL scopes fail closed to native composition");
    assert_true(
        vn_ime::SelectBrowserTextInputMode(
            true, false, search_scope) ==
            vn_ime::BrowserTextInputMode::NativeComposition &&
        vn_ime::SelectBrowserTextInputMode(
            true, false, default_scope) ==
            vn_ime::BrowserTextInputMode::NativeComposition &&
        vn_ime::SelectBrowserTextInputMode(
            true, false, email_scope) ==
            vn_ime::BrowserTextInputMode::NativeComposition,
        "Search, default, and email browser inputs retain native composition");
    assert_true(
        vn_ime::SelectBrowserTextInputMode(
            true, false, password_scope) ==
            vn_ime::BrowserTextInputMode::NativeComposition &&
        vn_ime::SelectBrowserTextInputMode(
            true, false, url_and_password) ==
            vn_ime::BrowserTextInputMode::NativeComposition,
        "Password scope wins over URL native-reconversion mode");

    using FocusRefreshPolicy =
        vn_ime::InputScopeFocusRefreshPolicy;
    assert_true(
        vn_ime::SelectInputScopeFocusRefreshPolicy(true) ==
            FocusRefreshPolicy::DeferToTextKeySyncOnly &&
        vn_ime::SelectInputScopeFocusRefreshPolicy(false) ==
            FocusRefreshPolicy::ImmediateSyncWithLegacyFallback,
        "Browser focus defers scope refresh while non-browser fallback stays unchanged");
    assert_true(
        vn_ime::ShouldRequestBrowserInputScopeCheck(
            true, true, true, true, false) &&
        vn_ime::ShouldRequestBrowserInputScopeCheck(
            true, true, false, false, false) &&
        !vn_ime::ShouldRequestBrowserInputScopeCheck(
            true, false, true, true, false) &&
        !vn_ime::ShouldRequestBrowserInputScopeCheck(
            true, true, true, true, true) &&
        vn_ime::ShouldRequestBrowserInputScopeCheck(
            true, true, true, false, true) &&
        !vn_ime::ShouldRequestBrowserInputScopeCheck(
            false, true, true, true, false),
        "Only a real browser text key checks pending state and replacement contexts are rechecked");

    const auto scope_check_success =
        vn_ime::DecideBrowserInputScopeCheck(
            true, true, true, true);
    assert_true(
        scope_check_success.continue_key &&
            scope_check_success.clear_pending &&
            !scope_check_success.clear_sensitive_state,
        "Successful synchronous browser scope check continues the first key");

    const auto scope_request_failure =
        vn_ime::DecideBrowserInputScopeCheck(
            true, false, false, false);
    const auto scope_execution_failure =
        vn_ime::DecideBrowserInputScopeCheck(
            true, true, true, false);
    assert_true(
        !scope_request_failure.continue_key &&
            !scope_request_failure.clear_pending &&
            scope_request_failure.clear_sensitive_state &&
        !scope_execution_failure.continue_key &&
            !scope_execution_failure.clear_pending &&
            scope_execution_failure.clear_sensitive_state,
        "Failed browser scope request or execution passes the key and clears sensitive state");

    const auto no_scope_check =
        vn_ime::DecideBrowserInputScopeCheck(
            false, false, false, false);
    assert_true(
        no_scope_check.continue_key &&
            !no_scope_check.clear_pending &&
            !no_scope_check.clear_sensitive_state,
        "Checked browser context does not gate later text keys");

    using UrlAction = vn_ime::BrowserUrlKeyAction;
    using TextMode = vn_ime::BrowserTextInputMode;
    assert_true(
        vn_ime::DecideBrowserUrlKeyAction(
            TextMode::UrlNativeReconversion, false, true, false) ==
            UrlAction::NativeHostKey &&
        vn_ime::DecideBrowserUrlKeyAction(
            TextMode::UrlNativeReconversion, false, false, false) ==
            UrlAction::NativeHostKey,
        "Literal URL keys and native boundaries stay host-owned at TestKeyDown");
    assert_true(
        vn_ime::DecideBrowserUrlKeyAction(
            TextMode::UrlNativeReconversion, false, true, true) ==
            UrlAction::ApplyTypedReconversion,
        "Only an actual transformed candidate is claimed");
    assert_true(
        vn_ime::DecideBrowserUrlKeyAction(
            TextMode::NativeComposition, false, true, true) ==
            UrlAction::NativeComposition &&
        vn_ime::DecideBrowserUrlKeyAction(
            TextMode::UrlNativeReconversion, true, true, true) ==
            UrlAction::NativeComposition,
        "Non-URL scopes and existing compositions keep the legacy path");

    const auto token_before_caret = [](std::wstring_view text) {
        size_t start = text.length();
        while (start > 0 && rules::IsWordChar(text[start - 1])) {
            --start;
        }
        return text.substr(start);
    };

    struct NativeUrlResult {
        std::wstring host_text;
        size_t native_key_count = 0;
        size_t readwrite_action_count = 0;
    };
    const auto run_native_url = [&](
        InputMethod method,
        std::wstring_view keys,
        CorrectionLevel correction = CorrectionLevel::Normal,
        EnglishProtectionLevel protection =
            EnglishProtectionLevel::Balanced) {
        NativeUrlResult result;
        for (const wchar_t ch : keys) {
            const std::wstring_view token =
                token_before_caret(result.host_text);
            auto candidate =
                BuildBrowserUrlTypedReconversionCandidate(
                    token, ch, method, correction, protection);
            const auto action = vn_ime::DecideBrowserUrlKeyAction(
                TextMode::UrlNativeReconversion, false, true,
                candidate.has_value());
            if (action == UrlAction::ApplyTypedReconversion) {
                result.host_text.resize(
                    result.host_text.length() - token.length());
                result.host_text.append(*candidate);
                ++result.readwrite_action_count;
            } else {
                result.host_text.push_back(ch);
                ++result.native_key_count;
            }
        }
        return result;
    };

    const NativeUrlResult gen =
        run_native_url(InputMethod::Telex, L"gen");
    assert_true(
        gen.native_key_count == 3 &&
            gen.readwrite_action_count == 0 &&
            gen.host_text == L"gen",
        "Browser URL gen has three native TestKeyDown decisions and zero READWRITE actions");

    const NativeUrlResult vni =
        run_native_url(InputMethod::VNI, L"te1");
    assert_true(
        vni.native_key_count == 2 &&
            vni.readwrite_action_count == 1,
        "VNI URL te keeps t/e native and claims only tone key 1");
    assert_eq(vni.host_text, L"t\u00E9",
              "VNI URL typed reconversion te1 -> te acute");

    const NativeUrlResult telex =
        run_native_url(InputMethod::Telex, L"tes");
    assert_true(
        telex.native_key_count == 2 &&
            telex.readwrite_action_count == 1,
        "Telex URL te keeps t/e native and claims only tone key s");
    assert_eq(telex.host_text, L"t\u00E9",
              "Telex URL typed reconversion tes -> te acute");

    assert_eq(run_native_url(InputMethod::Telex, L"tee").host_text,
              L"t\u00EA", "Telex URL second e transforms te -> te circumflex");
    assert_eq(run_native_url(InputMethod::Telex, L"dd").host_text,
              L"\u0111", "Telex URL second d transforms d -> d stroke");
    assert_eq(run_native_url(InputMethod::Telex, L"hoaf").host_text,
              L"ho\u00E0", "Telex URL tone modifier transforms hoa -> hoa grave");
    assert_eq(run_native_url(InputMethod::SimpleTelex, L"tes").host_text,
              L"t\u00E9", "SimpleTelex URL typed reconversion parity");

    const NativeUrlResult telex_tone_escape =
        run_native_url(InputMethod::Telex, L"tess");
    assert_eq(telex_tone_escape.host_text, L"tes",
              "Telex URL repeated tone key escapes the applied tone");
    assert_true(telex_tone_escape.readwrite_action_count == 2,
                "Telex URL tone escape is applied as a second transformation");

    const NativeUrlResult telex_shape_escape =
        run_native_url(InputMethod::Telex, L"aww");
    assert_eq(telex_shape_escape.host_text, L"aw",
              "Telex URL repeated shape key escapes the applied shape");

    const NativeUrlResult vni_tone_escape =
        run_native_url(InputMethod::VNI, L"a11");
    assert_eq(vni_tone_escape.host_text, L"a1",
              "VNI URL repeated tone digit escapes the applied tone");

    const NativeUrlResult invalid_domain =
        run_native_url(InputMethod::Telex, L"https");
    assert_true(invalid_domain.host_text == L"https" &&
                    invalid_domain.readwrite_action_count == 0,
                "Invalid URL token remains fully native");
    assert_true(
        !BuildBrowserUrlTypedReconversionCandidate(
            L"t\u00E9", L'h', InputMethod::Telex,
            CorrectionLevel::Normal,
            EnglishProtectionLevel::Balanced),
        "Diacritic token does not bypass validation for a non-escape key");
    assert_true(
        !BuildBrowserUrlTypedReconversionCandidate(
            L"t\u00E9h", L's', InputMethod::Telex,
            CorrectionLevel::Normal,
            EnglishProtectionLevel::Balanced),
        "Invalid accented URL token cannot use repeated modifier escape");

    const NativeUrlResult normal_key_correction =
        run_native_url(InputMethod::Telex, L"tuyetn");
    assert_eq(normal_key_correction.host_text, L"tuy\u1EC1n",
              "URL non-modifier n preserves Normal typo correction");
    assert_true(normal_key_correction.readwrite_action_count == 1,
                "URL non-modifier correction claims only its transforming key");

    const NativeUrlResult protected_balanced =
        run_native_url(InputMethod::Telex, L"res");
    const NativeUrlResult protected_english_first =
        run_native_url(InputMethod::Telex, L"res",
                       CorrectionLevel::Normal,
                       EnglishProtectionLevel::EnglishFirst);
    assert_true(
        protected_balanced.native_key_count == 3 &&
            protected_balanced.readwrite_action_count == 0 &&
            protected_balanced.host_text == L"res" &&
        protected_english_first.native_key_count == 3 &&
            protected_english_first.readwrite_action_count == 0 &&
            protected_english_first.host_text == L"res",
        "Balanced and English First keep re+s fully native");

    const auto vni_url_digit =
        BuildBrowserUrlTypedReconversionCandidate(
            L"win", L'1', InputMethod::VNI,
            CorrectionLevel::Normal,
            EnglishProtectionLevel::Balanced);
    assert_true(
        !vni_url_digit &&
        vn_ime::DecideBrowserUrlKeyAction(
            TextMode::UrlNativeReconversion, false, true, false) ==
            UrlAction::NativeHostKey,
        "VNI URL code digits remain native without a transformed candidate");

    assert_true(
        vn_ime::DecideBrowserUrlKeyAction(
            TextMode::UrlNativeReconversion, false, false, false) ==
            UrlAction::NativeHostKey,
        "URL path punctuation, Backspace, Space, and navigation stay native");

    const std::wstring long_token(
        kMaxRawKeysPerComposition + 1, L'a');
    const auto latency_start = std::chrono::steady_clock::now();
    size_t long_rejections = 0;
    for (size_t i = 0; i < 1000; ++i) {
        if (!BuildBrowserUrlTypedReconversionCandidate(
                long_token, L's', InputMethod::Telex,
                CorrectionLevel::Normal,
                EnglishProtectionLevel::Balanced)) {
            ++long_rejections;
        }
    }
    const auto latency_elapsed =
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - latency_start);
    assert_true(
        long_rejections == 1000 && latency_elapsed.count() < 100000,
        "Browser URL long-token guard rejects quickly");
}

void test_key_translation_without_state_mutation() {
    std::cout << "\nRunning test_key_translation_without_state_mutation..." << std::endl;

    BYTE keyboard_state[256]{};
    const HKL expected_layout =
        reinterpret_cast<HKL>(static_cast<ULONG_PTR>(0x1234));
    UINT observed_flags = 0;
    bool forwarded_arguments = false;
    const wchar_t translated =
        vn_ime::TranslateVirtualKeyWithoutStateMutation(
            static_cast<UINT>('A'), 0x1E, keyboard_state,
            expected_layout, false,
            [&](UINT virtual_key, UINT scan_code, const BYTE* state,
                LPWSTR buffer, int buffer_size, UINT flags, HKL layout) {
                observed_flags = flags;
                forwarded_arguments = virtual_key == static_cast<UINT>('A') &&
                    scan_code == 0x1E && state == keyboard_state &&
                    buffer_size == 4 && layout == expected_layout;
                buffer[0] = L'a';
                return 1;
            });
    assert_true(
        translated == L'a' && forwarded_arguments,
        "Key translation forwards layout/state and returns the translated character");
    assert_true(
        observed_flags == vn_ime::kToUnicodeDoNotChangeKeyboardState,
        "Key translation requests no mutation of the dead-key state");

    const wchar_t dead_key =
        vn_ime::TranslateVirtualKeyWithoutStateMutation(
            VK_OEM_7, 0, keyboard_state, expected_layout, false,
            [](UINT, UINT, const BYTE*, LPWSTR, int, UINT, HKL) {
                return -1;
            });
    assert_true(
        dead_key == L'\0',
        "Dead-key results remain host-owned instead of becoming IME text");

    const wchar_t numpad_digit =
        vn_ime::TranslateVirtualKeyWithoutStateMutation(
            VK_NUMPAD7, 0, keyboard_state, expected_layout, true,
            [](UINT, UINT, const BYTE*, LPWSTR, int, UINT, HKL) {
                return 0;
            });
    assert_true(
        numpad_digit == L'7',
        "NumLock fallback remains unchanged when translation returns no character");
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
    std::wstring long_token(kMaxRawKeysPerComposition + 1, L'a');
    assert_true(!rules::ResolveReconversionSpan(
                    long_token,
                    long_token.length() / 2,
                    long_token.length() / 2,
                    false,
                    false,
                    kMaxRawKeysPerComposition).has_value(),
                "Reject reconversion span that exceeds max token length");

    for (const InputMethod method : {InputMethod::Telex, InputMethod::SimpleTelex}) {
        const auto edit = BuildReconversionEdit(L"re", 2, 2, L's', method);
        assert_true(edit.has_value(),
                    "Explicit Vietnamese reconversion bypasses English protection");
        if (edit) {
            assert_eq(edit->replacement, L"r\u00E9",
                      "Committed re + s reconverts to Vietnamese re acute");
        }
    }
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
        {InputMethod::Telex, L"kroong", L"kr\u00F4ng", "Telex place name: kroong -> krong circumflex"},
        {InputMethod::Telex, L"Buks", L"B\u00FAk", "Telex place name: Buks -> Buk acute"},
        {InputMethod::VNI, L"Viet61", L"Vi\u1EBFt", "VNI uppercase mixed: Viet61 -> Viet"},
        {InputMethod::VNI, L"krong6", L"kr\u00F4ng", "VNI place name: krong6 -> krong circumflex"},
        {InputMethod::VNI, L"Bu1k", L"B\u00FAk", "VNI place name: Bu1k -> Buk acute"},
    };

    for (const auto& c : cases) {
        assert_engine_output(c.method, c.keys, c.expected, c.name);
    }

    assert_eq(type_text_committing_on_spaces(InputMethod::Telex, L"vietes nam"), L"vi\u1EBFt nam", "Multi-word: vietes nam");
    assert_eq(type_text_committing_on_spaces(InputMethod::Telex, L"github vietes"), L"github vi\u1EBFt", "Multi-word mixed English/Vietnamese");
    assert_eq(type_text_committing_on_spaces(InputMethod::VNI, L"Krong6 Bu1k"),
              L"Kr\u00F4ng B\u00FAk",
              "Multi-word VNI place name: Krong6 Bu1k");
    assert_eq(type_text_committing_on_spaces(InputMethod::Telex, L"Kroong Buks"),
              L"Kr\u00F4ng B\u00FAk",
              "Multi-word Telex place name: Kroong Buks");
}

void test_reconversion_ad_hoc_corpus() {
    std::cout << "\nRunning test_reconversion_ad_hoc_corpus..." << std::endl;

    auto apply_reconversion_key = [](std::wstring& text, size_t& caret, wchar_t key, InputMethod method,
                                     const std::string& test_name) {
        auto edit = BuildReconversionEdit(text, caret, caret, key, method);
        assert_true(edit.has_value(), test_name + " has edit");
        if (!edit) {
            return;
        }
        text.replace(edit->start, edit->end - edit->start, edit->replacement);
        caret = edit->start + edit->selection_start;
    };

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
    assert_candidate(L"thuo", L'6', InputMethod::VNI, L"thu\u00F4", "Ad-hoc reconversion VNI: thuo + 6");
    assert_candidate(L"thuoc", L'6', InputMethod::VNI, L"thu\u00F4c", "Ad-hoc reconversion VNI: thuoc + 6");
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

    assert_true(!BuildReconversionEdit(L"tay", 0, 0, L'c', InputMethod::Telex).has_value(),
                "Typed c before tay starts new text instead of reconverting tay");
    assert_true(!BuildReconversionEdit(L"ray", 0, 0, L'c', InputMethod::Telex).has_value(),
                "Typed c before ray starts new text instead of reconverting ray");
    assert_true(!BuildReconversionEdit(L"may", 0, 0, L'c', InputMethod::Telex).has_value(),
                "Typed c before may starts new text instead of reconverting may");
    assert_true(!BuildReconversionEdit(L"tay", 0, 3, L'c', InputMethod::Telex).has_value(),
                "Typed c over selected tay replaces selection instead of reconverting");

    auto start_tone_edit = BuildReconversionEdit(L"hoang", 0, 0, L'f', InputMethod::Telex);
    assert_true(start_tone_edit.has_value(), "Tone reconversion at token start remains enabled");
    if (start_tone_edit) {
        assert_eq(start_tone_edit->replacement, L"ho\u00E0ng", "Tone reconversion at token start replacement");
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

    auto final_u_edit = BuildReconversionEdit(L"h\u01B0", 2, 2, L'u', InputMethod::Telex);
    assert_true(final_u_edit.has_value(), "Typed u at token end still supports hư -> hưu reconversion");
    if (final_u_edit) {
        assert_eq(final_u_edit->replacement, L"h\u01B0u", "Typed u at token end replacement");
        assert_true(final_u_edit->selection_start == 3 && final_u_edit->selection_end == 3,
                    "Typed u at token end moves caret after inserted u");
    }

    std::wstring vni_viet = L"v\u00EDt";
    size_t vni_viet_caret = 2;
    apply_reconversion_key(vni_viet, vni_viet_caret, L'e', InputMethod::VNI,
                           "VNI insert e before final t in vit");
    apply_reconversion_key(vni_viet, vni_viet_caret, L'6', InputMethod::VNI,
                           "VNI apply circumflex after inserted e in viet");
    assert_eq(vni_viet, L"vi\u1EBFt", "VNI caret edit: vit + e + 6 -> viet");

    std::wstring vni_doan = L"\u0111\u00F2n";
    size_t vni_doan_caret = 2;
    apply_reconversion_key(vni_doan, vni_doan_caret, L'a', InputMethod::VNI,
                           "VNI insert a before final n in don");
    assert_eq(vni_doan, L"\u0111o\u00E0n", "VNI caret edit: don + a -> doan");

    std::wstring vni_tien = L"t\u00EDn";
    size_t vni_tien_caret = 2;
    apply_reconversion_key(vni_tien, vni_tien_caret, L'e', InputMethod::VNI,
                           "VNI insert e before final n in tin");
    apply_reconversion_key(vni_tien, vni_tien_caret, L'6', InputMethod::VNI,
                           "VNI apply circumflex after inserted e in tien");
    assert_eq(vni_tien, L"ti\u1EBFn", "VNI caret edit: tin + e + 6 -> tien");

    std::wstring vni_upper_viet = L"V\u00EDt";
    size_t vni_upper_viet_caret = 2;
    apply_reconversion_key(vni_upper_viet, vni_upper_viet_caret, L'e', InputMethod::VNI,
                           "VNI insert e before final t in uppercase Vit");
    apply_reconversion_key(vni_upper_viet, vni_upper_viet_caret, L'6', InputMethod::VNI,
                           "VNI apply circumflex after inserted e in uppercase Viet");
    assert_eq(vni_upper_viet, L"Vi\u1EBFt", "VNI caret edit: Vit + e + 6 -> Viet");

    // "gửi" reconversion test case
    std::wstring vni_gui = L"g\u1EEDi"; // gửi
    size_t vni_gui_caret = 3;
    apply_reconversion_key(vni_gui, vni_gui_caret, L'1', InputMethod::VNI,
                           "VNI change tone of gửi to acute");
    assert_eq(vni_gui, L"g\u1EE9i", "VNI caret edit: gửi + 1 -> gứi");

    apply_reconversion_key(vni_gui, vni_gui_caret, L'0', InputMethod::VNI,
                           "VNI clear tone of gứi");
    assert_eq(vni_gui, L"g\u01B0i", "VNI caret edit: gứi + 0 -> gưi");

    // Start of word reconversion prevention tests
    assert_true(!BuildReconversionEdit(L"chu\u1ED7i", 0, 0, L'1', InputMethod::VNI).has_value(),
                "Typed VNI 1 before chuỗi does not trigger reconversion");
    assert_true(BuildReconversionEdit(L"chu\u1ED7i", 0, 0, L's', InputMethod::Telex).has_value(),
                "Typed Telex s before chuỗi triggers reconversion");

    std::wstring telex_viet = L"v\u00EDt";
    size_t telex_viet_caret = 2;
    apply_reconversion_key(telex_viet, telex_viet_caret, L'e', InputMethod::Telex,
                           "Telex insert e before final t in vit");
    apply_reconversion_key(telex_viet, telex_viet_caret, L'e', InputMethod::Telex,
                           "Telex apply circumflex after inserted e in viet");
    assert_eq(telex_viet, L"vi\u1EBFt", "Telex caret edit: vit + e + e -> viet");

    std::wstring telex_doan = L"\u0111\u00F2n";
    size_t telex_doan_caret = 2;
    apply_reconversion_key(telex_doan, telex_doan_caret, L'a', InputMethod::Telex,
                           "Telex insert a before final n in don");
    assert_eq(telex_doan, L"\u0111o\u00E0n", "Telex caret edit: don + a -> doan");
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
    assert_true(ClassifyExcelFormulaPrefix(L"") == ExcelFormulaInputKind::NotFormula,
                "Excel empty cell prefix is not formula syntax");
    assert_true(ClassifyExcelFormulaPrefix(L"   ") == ExcelFormulaInputKind::NotFormula,
                "Excel whitespace only prefix is not formula syntax");
    assert_true(ClassifyExcelFormulaPrefix(L"SUM(A1)") == ExcelFormulaInputKind::NotFormula,
                "Excel formula name without equals is not formula syntax");
    assert_true(ClassifyExcelFormulaPrefix(L"x = y") == ExcelFormulaInputKind::NotFormula,
                "Excel equation with equals in middle is not formula syntax");
    assert_true(ClassifyExcelFormulaPrefix(L" =SUM(A1)") == ExcelFormulaInputKind::FormulaSyntax,
                "Excel formula with leading space before equals is formula syntax");
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

    assert_true(
        ShouldStartExcelFormulaAtEntry(true),
        "Excel equals as the first printable entry starts formula mode");
    assert_true(
        !ShouldStartExcelFormulaAtEntry(false),
        "Excel equals after locally observed cell text stays ordinary text");

    assert_true(
        ShouldReenterExcelQuotedTextOnBackspace(true, 0),
        "Excel Backspace over the closing quote of an empty string re-enters quoted text");
    assert_true(
        !ShouldReenterExcelQuotedTextOnBackspace(true, 1),
        "Excel Backspace first removes formula syntax following a closed string");
    assert_true(
        !ShouldReenterExcelQuotedTextOnBackspace(false, 0),
        "Excel formula syntax without a closed string does not enter quoted text");

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
    using vn_ime::AppInputProfileOrigin;

    vn_ime::IMEConfig defaults;
    assert_true(defaults.enable_app_blocklist, "Blocklist defaults to enabled for terminal native input");
    assert_true(defaults.enable_auto_exclude, "Auto-exclude defaults to enabled");
    assert_true(defaults.auto_blocked_apps.empty(), "Auto-blocked apps list is empty by default");
    assert_true(defaults.blocked_apps.empty(), "Blocked apps list is empty by default to support terminal apps");
    assert_true(defaults.enable_app_input_profiles &&
                    defaults.enable_auto_app_input_profiles &&
                    defaults.app_input_profiles.empty(),
                "Per-app profiles and automatic profile migration default safely enabled with no rules");
    assert_true(vn_ime::IsBuiltInNativeBypassProcess(L"taskmgr.exe"), "Task Manager is a built-in native bypass process");
    assert_true(vn_ime::IsBuiltInNativeBypassProcess(L"C:\\Windows\\System32\\Taskmgr.EXE"), "Task Manager path is normalized for built-in bypass");
    assert_true(!vn_ime::IsBuiltInNativeBypassProcess(L"notepad.exe"), "Notepad is not a built-in native bypass process");
    assert_true(!vn_ime::IsBuiltInNativeBypassProcess(L"explorer.exe"), "Explorer is not a built-in native bypass process");
    assert_true(!vn_ime::IsBuiltInNativeBypassProcess(L"winword.exe"), "Word is not a built-in native bypass process");
    assert_true(vn_ime::ShouldTreatShellSurfaceAsNative(false, true), "Shell file list without Edit focus stays native");
    assert_true(!vn_ime::ShouldTreatShellSurfaceAsNative(true, true), "Shell inline rename Edit is not native-bypassed");
    assert_true(!vn_ime::ShouldTreatShellSurfaceAsNative(false, false), "Non-shell text input is not native-bypassed");
    assert_true(vn_ime::ShouldUseNotepadPlusPlusDirectInline(L"notepad++.exe", L"Edit"),
                "Notepad++ Find/Replace Edit fields use direct inline replacement");
    assert_true(vn_ime::ShouldUseNotepadPlusPlusDirectInline(L"C:\\Tools\\Notepad++.EXE", L"Scintilla"),
                "Notepad++ main Scintilla editor uses direct inline replacement");
    assert_true(!vn_ime::ShouldUseNotepadPlusPlusDirectInline(L"notepad.exe", L"Edit"),
                "Plain Notepad Edit fields keep existing TSF behavior");
    assert_true(!vn_ime::ShouldUseNotepadPlusPlusDirectInline(L"notepad++.exe", L"ComboBox"),
                "Other Notepad++ controls keep existing TSF behavior");
    assert_true(vn_ime::ShouldCommitNotepadPlusPlusDirectInlineBoundary(L"notepad++.exe", L"Scintilla", L' '),
                "Notepad++ Scintilla direct inline commits native space boundary");
    assert_true(vn_ime::ShouldCommitNotepadPlusPlusDirectInlineBoundary(L"notepad++.exe", L"Edit", L' '),
                "Notepad++ Find/Replace direct inline commits native space boundary");
    assert_true(!vn_ime::ShouldCommitNotepadPlusPlusDirectInlineBoundary(L"notepad++.exe", L"Scintilla", L'a'),
                "Notepad++ direct inline letters are not commit boundaries");
    assert_true(!vn_ime::ShouldCommitNotepadPlusPlusDirectInlineBoundary(L"notepad.exe", L"Edit", L' '),
                "Plain Notepad space keeps existing behavior");
    assert_true(vn_ime::CanContinueScintillaDirectInline(true, 10, 16, 16),
                "Scintilla direct inline continues from fixed anchor after multibyte replacement");
    assert_true(!vn_ime::CanContinueScintillaDirectInline(true, 10, 9, 9),
                "Scintilla direct inline resets if caret moves before fixed anchor");
    assert_true(!vn_ime::CanContinueScintillaDirectInline(true, 10, 12, 13),
                "Scintilla direct inline resets on non-empty selection");

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

    std::vector<std::wstring> direct_apps = vn_ime::ParseDirectAppsListText(
        L"notepad.exe\r\n"
        L"explorer.exe:commit\r\n"
        L"notepad.exe:commit\r\n"
        L"anotherapp.exe:invalid\r\n"
    );
    assert_true(direct_apps.size() == 3, "Direct apps parser deduplicates by normalized process name");
    assert_eq(vn_ime::ProcessListToText(direct_apps), L"notepad.exe:inline\r\nexplorer.exe:commit\r\nanotherapp.exe:inline", "Direct apps formatting");

    vn_ime::IMEConfig disabled_auto_exclude;
    disabled_auto_exclude.enable_auto_exclude = false;
    disabled_auto_exclude.blocked_apps = {L"manual.exe"};
    bool changed = vn_ime::AutoExcludeApp(disabled_auto_exclude, L"Code.exe");
    assert_true(!changed, "Auto-exclude disabled does not mutate blocklist");
    assert_eq(vn_ime::ProcessListToText(disabled_auto_exclude.blocked_apps), L"manual.exe",
              "Auto-exclude disabled preserves manual blocklist");

    vn_ime::IMEConfig auto_exclude_new;
    changed = vn_ime::AutoExcludeApp(auto_exclude_new, L"C:\\Tools\\Code.EXE");
    assert_true(changed, "Auto-exclude adds new app");
    assert_eq(vn_ime::ProcessListToText(auto_exclude_new.blocked_apps), L"code.exe",
              "Auto-exclude normalizes blocked app name");
    assert_eq(vn_ime::ProcessListToText(auto_exclude_new.auto_blocked_apps), L"code.exe",
              "Auto-exclude marks ownership in auto list");
    const auto auto_excluded_profile = vn_ime::LookupAppInputProfile(
        auto_exclude_new.app_input_profiles, L"code.exe");
    assert_true(auto_excluded_profile.has_value() &&
                    !auto_excluded_profile->enabled &&
                    auto_excluded_profile->preferred_method == InputMethod::VNI &&
                    auto_excluded_profile->origin ==
                        AppInputProfileOrigin::Automatic,
                "Auto-exclude creates an Automatic disabled profile");
    changed = vn_ime::AutoExcludeApp(auto_exclude_new, L"CODE.EXE");
    assert_true(!changed, "Auto-exclude ignores duplicate path/case variant");
    assert_eq(vn_ime::ProcessListToText(auto_exclude_new.blocked_apps), L"code.exe",
              "Auto-exclude duplicate does not duplicate blocklist");

    vn_ime::IMEConfig manual_block;
    manual_block.blocked_apps = {L"code.exe"};
    changed = vn_ime::AutoExcludeApp(manual_block, L"code.exe");
    assert_true(changed,
                "Auto-exclude reports the new disabled profile for a manual block");
    assert_true(manual_block.auto_blocked_apps.empty(), "Manual block is not added to auto-owned list");
    const auto manual_block_profile = vn_ime::LookupAppInputProfile(
        manual_block.app_input_profiles, L"code.exe");
    assert_true(manual_block_profile.has_value() &&
                    !manual_block_profile->enabled &&
                    manual_block_profile->origin ==
                        AppInputProfileOrigin::Manual,
                "Manual block is mirrored into the authoritative profile model");

    vn_ime::IMEConfig auto_include_owned;
    auto_include_owned.blocked_apps = {L"code.exe", L"notepad.exe"};
    auto_include_owned.auto_blocked_apps = {L"code.exe"};
    auto_include_owned.app_input_profiles = {
        {L"code.exe", false, InputMethod::SimpleTelex,
         AppInputProfileOrigin::Automatic},
    };
    changed = vn_ime::AutoIncludeApp(auto_include_owned, L"C:\\Tools\\Code.EXE");
    assert_true(changed, "Auto-include removes auto-owned app");
    assert_eq(vn_ime::ProcessListToText(auto_include_owned.blocked_apps), L"notepad.exe",
              "Auto-include keeps unrelated blocked apps");
    assert_true(auto_include_owned.auto_blocked_apps.empty(), "Auto-include removes auto ownership marker");
    const auto auto_included_profile = vn_ime::LookupAppInputProfile(
        auto_include_owned.app_input_profiles, L"code.exe");
    assert_true(auto_included_profile.has_value() &&
                    auto_included_profile->enabled &&
                    auto_included_profile->preferred_method ==
                        InputMethod::SimpleTelex &&
                    auto_included_profile->origin ==
                        AppInputProfileOrigin::Automatic,
                "Auto-include re-enables Automatic Off without changing its method");

    vn_ime::IMEConfig auto_include_manual;
    auto_include_manual.blocked_apps = {L"code.exe"};
    auto_include_manual.app_input_profiles = {
        {L"code.exe", false, InputMethod::VNI,
         AppInputProfileOrigin::Manual},
    };
    changed = vn_ime::AutoIncludeApp(auto_include_manual, L"code.exe");
    assert_true(!changed,
                "Auto-include does not claim or enable a Manual Off rule");
    assert_eq(vn_ime::ProcessListToText(auto_include_manual.blocked_apps), L"code.exe",
              "Manual block survives auto-include");
    const auto included_manual_profile = vn_ime::LookupAppInputProfile(
        auto_include_manual.app_input_profiles, L"code.exe");
    assert_true(included_manual_profile.has_value() &&
                    !included_manual_profile->enabled &&
                    included_manual_profile->preferred_method == InputMethod::VNI &&
                    included_manual_profile->origin ==
                        AppInputProfileOrigin::Manual,
                "Auto-include keeps a manual block disabled in profiles");

    vn_ime::IMEConfig disabled_auto_include;
    disabled_auto_include.enable_auto_exclude = false;
    disabled_auto_include.blocked_apps = {L"code.exe"};
    disabled_auto_include.auto_blocked_apps = {L"code.exe"};
    changed = vn_ime::AutoIncludeApp(disabled_auto_include, L"code.exe");
    assert_true(!changed, "Auto-include disabled does not mutate blocklist");
    assert_eq(vn_ime::ProcessListToText(disabled_auto_include.blocked_apps), L"code.exe",
              "Auto-include disabled preserves blocked app");
    assert_eq(vn_ime::ProcessListToText(disabled_auto_include.auto_blocked_apps), L"code.exe",
              "Auto-include disabled preserves auto marker");

    std::vector<std::wstring> preserved_auto = vn_ime::PreserveAutoBlockedAppsForBlocklist(
        {L"code.exe", L"notepad.exe"},
        {L"notepad.exe", L"manual.exe"});
    assert_eq(vn_ime::ProcessListToText(preserved_auto), L"notepad.exe",
              "Config app preserves only auto markers still present in blocklist");
}

void test_app_input_profile_helpers() {
    std::cout << "\nRunning test_app_input_profile_helpers..." << std::endl;
    using vn_ime::AppInputMode;
    using vn_ime::AppInputProfile;
    using vn_ime::AppInputProfileOrigin;

    std::vector<AppInputProfile> profiles;
    assert_true(vn_ime::UpsertAppInputMode(
                    profiles, L"C:\\Apps\\Telex.EXE",
                    AppInputMode::Telex, InputMethod::VNI),
                "Per-app mode inserts Telex");
    assert_true(vn_ime::UpsertAppInputMode(
                    profiles, L"simple.exe",
                    AppInputMode::SimpleTelex, InputMethod::VNI),
                "Per-app mode inserts Simple Telex");
    assert_true(vn_ime::UpsertAppInputMode(
                    profiles, L"vni.exe",
                    AppInputMode::VNI, InputMethod::Telex),
                "Per-app mode inserts VNI");
    assert_true(vn_ime::UpsertAppInputMode(
                    profiles, L"off.exe",
                    AppInputMode::Off, InputMethod::SimpleTelex),
                "Per-app mode inserts Off with a retained fallback method");

    assert_true(profiles.size() == 4, "All four flat per-app modes are represented");
    assert_true(profiles[0].origin == AppInputProfileOrigin::Manual &&
                    profiles[3].origin == AppInputProfileOrigin::Manual,
                "Configuration APIs create Manual profiles by default");
    assert_true(vn_ime::AppInputModeForProfile(profiles[0]) == AppInputMode::Telex,
                "Telex profile flattens to Telex mode");
    assert_true(vn_ime::AppInputModeForProfile(profiles[1]) == AppInputMode::SimpleTelex,
                "Simple Telex profile flattens to Simple Telex mode");
    assert_true(vn_ime::AppInputModeForProfile(profiles[2]) == AppInputMode::VNI,
                "VNI profile flattens to VNI mode");
    assert_true(vn_ime::AppInputModeForProfile(profiles[3]) == AppInputMode::Off &&
                    profiles[3].preferred_method == InputMethod::SimpleTelex,
                "Off profile retains its preferred method");

    const auto inherited = vn_ime::ResolveAppInputProfile(
        profiles, L"missing.exe", InputMethod::SimpleTelex);
    assert_true(!inherited.has_explicit_profile && inherited.enabled &&
                    inherited.input_method == InputMethod::SimpleTelex,
                "Missing profile inherits the global input method");
    const auto inherited_disabled = vn_ime::ResolveAppInputProfile(
        profiles, L"missing.exe", false, InputMethod::Telex);
    assert_true(!inherited_disabled.has_explicit_profile &&
                    !inherited_disabled.enabled &&
                    inherited_disabled.input_method == InputMethod::Telex,
                "Missing profile inherits disabled global English state");
    const auto explicit_overrides_disabled_global =
        vn_ime::ResolveAppInputProfile(
            profiles, L"telex.exe", false, InputMethod::VNI);
    assert_true(explicit_overrides_disabled_global.has_explicit_profile &&
                    explicit_overrides_disabled_global.enabled &&
                    explicit_overrides_disabled_global.input_method ==
                        InputMethod::Telex,
                "Explicit profile overrides global enabled state and method");

    std::vector<AppInputProfile> retained_method = {
        {L"Editor.EXE", true, InputMethod::VNI},
    };
    assert_true(vn_ime::UpsertAppInputMode(
                    retained_method, L"editor.exe",
                    AppInputMode::Off, InputMethod::Telex),
                "VNI profile can be switched Off");
    assert_true(!retained_method[0].enabled &&
                    retained_method[0].preferred_method == InputMethod::VNI,
                "Switching Off does not erase preferred VNI");
    assert_true(vn_ime::ToggleAppInputProfileEnabled(
                    retained_method, L"EDITOR.EXE", InputMethod::Telex),
                "Disabled profile can be toggled back on");
    assert_true(retained_method[0].enabled &&
                    retained_method[0].preferred_method == InputMethod::VNI,
                "VNI -> Off -> enabled returns to VNI");

    std::vector<AppInputProfile> automatic_profile;
    assert_true(vn_ime::UpsertAppInputMode(
                    automatic_profile, L"auto.exe", AppInputMode::Off,
                    InputMethod::VNI, AppInputProfileOrigin::Automatic),
                "Runtime caller can create an Automatic Off profile");
    assert_true(!automatic_profile[0].enabled &&
                    automatic_profile[0].origin ==
                        AppInputProfileOrigin::Automatic,
                "Automatic Off is distinct from Manual Off");
    assert_true(vn_ime::UpsertAppInputMode(
                    automatic_profile, L"auto.exe", AppInputMode::VNI,
                    InputMethod::Telex),
                "Origin-neutral update can change an Automatic profile");
    assert_true(automatic_profile[0].enabled &&
                    automatic_profile[0].origin ==
                        AppInputProfileOrigin::Automatic,
                "Origin-neutral update preserves existing ownership");
    assert_true(vn_ime::SetAppInputProfileEnabled(
                    automatic_profile, L"auto.exe", false,
                    InputMethod::Telex, AppInputProfileOrigin::Manual),
                "Explicit Manual caller can claim an existing rule");
    assert_true(!automatic_profile[0].enabled &&
                    automatic_profile[0].origin ==
                        AppInputProfileOrigin::Manual,
                "Explicit origin changes ownership without losing method");

    const std::vector<AppInputProfile> deduplicated =
        vn_ime::NormalizeAppInputProfiles({
            {L"C:\\Old\\Code.EXE", true, InputMethod::Telex},
            {L"other.exe", true, InputMethod::SimpleTelex},
            {L" code.exe ", false, InputMethod::VNI},
        });
    assert_true(deduplicated.size() == 2 &&
                    deduplicated[1].process_name == L"code.exe" &&
                    !deduplicated[1].enabled &&
                    deduplicated[1].preferred_method == InputMethod::VNI,
                "Normalization deduplicates with the last explicit rule winning");

    const auto resolved_code = vn_ime::ResolveAppInputProfile(
        deduplicated, L"C:\\Tools\\CODE.exe", InputMethod::SimpleTelex);
    assert_true(resolved_code.has_explicit_profile && !resolved_code.enabled &&
                    resolved_code.input_method == InputMethod::VNI,
                "Lookup normalizes path and case");

    const auto direct_duplicate_lookup = vn_ime::LookupAppInputProfile(
        {
            {L"C:\\Old\\Editor.EXE", true, InputMethod::Telex},
            {L" editor.exe ", false, InputMethod::VNI},
        },
        L"EDITOR.EXE");
    assert_true(direct_duplicate_lookup.has_value() &&
                    direct_duplicate_lookup->process_name == L"editor.exe" &&
                    !direct_duplicate_lookup->enabled &&
                    direct_duplicate_lookup->preferred_method == InputMethod::VNI,
                "Direct lookup normalizes records and uses the last explicit rule");

    std::vector<AppInputProfile> removable = deduplicated;
    assert_true(vn_ime::RemoveAppInputProfile(removable, L"OTHER.EXE") &&
                    removable.size() == 1,
                "Remove profile uses normalized process name");
    assert_true(!vn_ime::RemoveAppInputProfile(removable, L"missing.exe"),
                "Removing an inherited app is a no-op");

    assert_true(vn_ime::IsConfigurableAppProcessName(
                    L"C:\\Tools\\Editor.EXE") &&
                    !vn_ime::IsConfigurableAppProcessName(L"") &&
                    !vn_ime::IsConfigurableAppProcessName(L"notes.txt") &&
                    !vn_ime::IsConfigurableAppProcessName(
                        L"C:\\Windows\\explorer.exe") &&
                    !vn_ime::IsConfigurableAppProcessName(
                        L"NEOKEY_CONFIG.EXE") &&
                    !vn_ime::IsConfigurableAppProcessName(
                        L"searchhost.exe") &&
                    !vn_ime::IsConfigurableAppProcessName(
                        L"StartMenuExperienceHost.exe"),
                "Per-app UI accepts apps and rejects protected system processes");

    std::vector<AppInputProfile> manual_row = {
        {L"row.exe", true, InputMethod::VNI,
         AppInputProfileOrigin::Automatic},
    };
    assert_true(vn_ime::UpsertManualAppInputMode(
                    manual_row, L"ROW.EXE", AppInputMode::Off,
                    InputMethod::Telex),
                "Per-app UI can set an existing row to Manual Off");
    const auto manual_off_row = vn_ime::LookupAppInputProfile(
        manual_row, L"row.exe");
    assert_true(manual_off_row.has_value() &&
                    !manual_off_row->enabled &&
                    manual_off_row->preferred_method == InputMethod::VNI &&
                    manual_off_row->origin == AppInputProfileOrigin::Manual,
                "Manual Off keeps the row preferred method and claims ownership");
    assert_true(vn_ime::RemoveAppInputProfile(manual_row, L"row.exe"),
                "Removing a per-app row succeeds");
    const auto removed_row = vn_ime::ResolveAppInputProfile(
        manual_row, L"row.exe", true, InputMethod::SimpleTelex);
    assert_true(!removed_row.has_explicit_profile && removed_row.enabled &&
                    removed_row.input_method == InputMethod::SimpleTelex,
                "Removing a row restores global inheritance instead of Off");

    const std::vector<AppInputProfile> roundtrip_source = {
        {L"Code.EXE", false, InputMethod::VNI,
         AppInputProfileOrigin::Automatic},
        {L"notepad.exe", true, InputMethod::SimpleTelex,
         AppInputProfileOrigin::Manual},
        {L"chrome.exe", true, InputMethod::Telex,
         AppInputProfileOrigin::Automatic},
    };
    const vn_ime::AppInputProfilesSerializeResult serialized =
        vn_ime::SerializeAppInputProfiles(roundtrip_source);
    const vn_ime::AppInputProfilesParseResult roundtrip =
        vn_ime::ParseAppInputProfiles(serialized.records);
    assert_true(serialized.success && roundtrip.schema_valid &&
                    !roundtrip.limit_exceeded &&
                    roundtrip.invalid_records == 0 &&
                    roundtrip.profiles ==
                        vn_ime::NormalizeAppInputProfiles(roundtrip_source),
                "Versioned REG_MULTI_SZ records round-trip all profile fields");

    std::vector<AppInputProfile> maximum_profiles;
    maximum_profiles.reserve(vn_ime::MAX_APP_INPUT_PROFILE_RULES);
    for (size_t i = 0; i < vn_ime::MAX_APP_INPUT_PROFILE_RULES; ++i) {
        const std::wstring suffix = std::to_wstring(i) + L".exe";
        std::wstring process_name(
            vn_ime::MAX_APP_INPUT_PROFILE_PROCESS_NAME_CHARS -
                suffix.length(),
            L'a');
        process_name += suffix;
        maximum_profiles.push_back({
            std::move(process_name), (i % 2) == 0, InputMethod::VNI,
            (i % 2) == 0
                ? AppInputProfileOrigin::Manual
                : AppInputProfileOrigin::Automatic});
    }
    const auto maximum_serialized =
        vn_ime::SerializeAppInputProfiles(maximum_profiles);
    const auto maximum_roundtrip = vn_ime::ParseAppInputProfiles(
        maximum_serialized.records);
    assert_true(maximum_serialized.success &&
                    maximum_serialized.records.size() ==
                        vn_ime::MAX_APP_INPUT_PROFILE_RULES + 1 &&
                    maximum_serialized.serialized_chars ==
                        vn_ime::MAX_APP_INPUT_PROFILES_SERIALIZED_CHARS &&
                    maximum_roundtrip.schema_valid &&
                    !maximum_roundtrip.limit_exceeded &&
                    maximum_roundtrip.profiles == maximum_profiles,
                "Maximum profile set round-trips without a partial prefix");
    assert_true(vn_ime::RawMultiStringCharCount(
                    maximum_serialized.records) ==
                    maximum_serialized.serialized_chars,
                "Serialized size includes the final REG_MULTI_SZ NUL");

    std::vector<AppInputProfile> too_many_profiles = maximum_profiles;
    too_many_profiles.push_back(
        {L"overflow.exe", true, InputMethod::Telex,
         AppInputProfileOrigin::Manual});
    const auto rejected_serialization =
        vn_ime::SerializeAppInputProfiles(too_many_profiles);
    assert_true(!rejected_serialization.success &&
                    rejected_serialization.records.empty(),
                "Serializer rejects overflow instead of writing a prefix");

    const vn_ime::AppInputProfilesParseResult malformed =
        vn_ime::ParseAppInputProfiles({
            std::wstring(vn_ime::APP_INPUT_PROFILES_SCHEMA_V1),
            L"valid.exe\t1\t0\t0",
            L"missing-fields",
            L"bad-enabled.exe\t2\t1\t0",
            L"bad-method.exe\t1\t9\t0",
            L"bad-origin.exe\t1\t0\t9",
            L"tab\tinside.exe\t1\t0\t0",
            L"VALID.EXE\t0\t2\t1",
        });
    assert_true(malformed.schema_valid && malformed.invalid_records == 5 &&
                    malformed.duplicate_records == 1 &&
                    malformed.profiles.size() == 1 &&
                    !malformed.profiles[0].enabled &&
                    malformed.profiles[0].preferred_method == InputMethod::VNI &&
                    malformed.profiles[0].origin ==
                        AppInputProfileOrigin::Automatic,
                "Parser rejects malformed origin and applies last valid duplicate");

    const auto wrong_schema = vn_ime::ParseAppInputProfiles({
        L"neokey.app-input-profiles\t99", L"code.exe\t1\t0\t0"});
    assert_true(!wrong_schema.schema_valid && wrong_schema.profiles.empty(),
                "Unknown persistence schema fails closed");

    std::vector<std::wstring> too_many_records(
        vn_ime::MAX_APP_INPUT_PROFILE_RULES + 2,
        L"app.exe\t1\t0\t0");
    too_many_records[0] = std::wstring(vn_ime::APP_INPUT_PROFILES_SCHEMA_V1);
    const auto oversized_count = vn_ime::ParseAppInputProfiles(too_many_records);
    assert_true(oversized_count.schema_valid && oversized_count.limit_exceeded &&
                    oversized_count.profiles.empty(),
                "Parser rejects profile counts above the bounded limit");

    std::wstring oversized_record(
        vn_ime::MAX_APP_INPUT_PROFILE_RECORD_CHARS + 1, L'a');
    const auto oversized_length = vn_ime::ParseAppInputProfiles({
        std::wstring(vn_ime::APP_INPUT_PROFILES_SCHEMA_V1), oversized_record});
    assert_true(oversized_length.schema_valid && oversized_length.limit_exceeded &&
                    oversized_length.profiles.empty(),
                "Parser rejects oversized profile records");

    const std::vector<AppInputProfile> migrated =
        vn_ime::MigrateAppInputProfiles(
            {{L"Chrome.EXE", true, InputMethod::Telex,
              AppInputProfileOrigin::Manual}},
            {L"chrome.exe", L"WindowsTerminal.EXE", L"manual.exe",
             L"auto.exe"},
            {L"windowsterminal.exe", L"auto.exe"},
            {
                {L"chrome.exe", 1},
                {L"windowsterminal.exe", 0},
                {L"code.exe", 1},
                {L"notepad.exe", 0},
                {L"invalid.exe", 7},
            },
            InputMethod::VNI);
    const auto chrome = vn_ime::LookupAppInputProfile(migrated, L"chrome.exe");
    const auto terminal = vn_ime::LookupAppInputProfile(
        migrated, L"windowsterminal.exe");
    const auto code = vn_ime::LookupAppInputProfile(migrated, L"code.exe");
    const auto notepad = vn_ime::LookupAppInputProfile(migrated, L"notepad.exe");
    const auto manual_block = vn_ime::LookupAppInputProfile(
        migrated, L"manual.exe");
    const auto automatic_block = vn_ime::LookupAppInputProfile(
        migrated, L"auto.exe");
    assert_true(chrome.has_value() && chrome->enabled &&
                    chrome->preferred_method == InputMethod::Telex &&
                    chrome->origin == AppInputProfileOrigin::Manual,
                "New profile wins over legacy state and keeps its origin");
    assert_true(terminal.has_value() && terminal->enabled &&
                    terminal->preferred_method == InputMethod::VNI &&
                    terminal->origin == AppInputProfileOrigin::Automatic,
                "Explicit legacy AppTypingMode overrides migrated BlockedApps state");
    assert_true(code.has_value() && !code->enabled &&
                    code->preferred_method == InputMethod::VNI &&
                    code->origin == AppInputProfileOrigin::Automatic &&
                    notepad.has_value() && notepad->enabled &&
                    notepad->preferred_method == InputMethod::VNI &&
                    notepad->origin == AppInputProfileOrigin::Automatic,
                "Legacy AppTypingMode migrates as Automatic with the global method");
    assert_true(manual_block.has_value() && !manual_block->enabled &&
                    manual_block->origin == AppInputProfileOrigin::Manual &&
                    automatic_block.has_value() &&
                    !automatic_block->enabled &&
                    automatic_block->origin ==
                        AppInputProfileOrigin::Automatic,
                "Legacy block ownership migrates from AutoBlockedApps");
    assert_true(!vn_ime::LookupAppInputProfile(migrated, L"invalid.exe").has_value(),
                "Invalid legacy typing mode is rejected");
    assert_eq(vn_ime::ProcessListToText(
                  vn_ime::DeriveLegacyBlockedApps(migrated)),
              L"manual.exe\r\nauto.exe\r\ncode.exe",
              "Legacy BlockedApps is derived only from disabled profiles");
    assert_eq(vn_ime::ProcessListToText(
                  vn_ime::DeriveLegacyAutoBlockedApps(migrated)),
              L"auto.exe\r\ncode.exe",
              "Legacy AutoBlockedApps is derived only from disabled Automatic profiles");

    const auto authoritative_empty = vn_ime::ResolveLoadedAppInputProfiles(
        true, {}, {L"blocked.exe"}, {L"blocked.exe"},
        {{L"legacy.exe", 1}}, InputMethod::VNI);
    assert_true(authoritative_empty.empty(),
                "Authoritative schema-only profile source ignores all legacy entries");

    const auto authoritative_after_remove =
        vn_ime::ResolveLoadedAppInputProfiles(
            true,
            {{L"kept.exe", true, InputMethod::SimpleTelex,
              AppInputProfileOrigin::Manual}},
            {L"removed.exe"}, {L"removed.exe"},
            {{L"removed.exe", 1}}, InputMethod::VNI);
    assert_true(authoritative_after_remove.size() == 1 &&
                    vn_ime::LookupAppInputProfile(
                        authoritative_after_remove, L"kept.exe").has_value() &&
                    !vn_ime::LookupAppInputProfile(
                         authoritative_after_remove, L"removed.exe").has_value(),
                "Authoritative source does not resurrect a removed legacy app");

    const auto absent_source_migrates =
        vn_ime::ResolveLoadedAppInputProfiles(
            false, {}, {L"blocked.exe"}, {L"blocked.exe"},
            {{L"legacy.exe", 1}}, InputMethod::Telex);
    const auto migrated_blocked = vn_ime::LookupAppInputProfile(
        absent_source_migrates, L"blocked.exe");
    const auto migrated_typing = vn_ime::LookupAppInputProfile(
        absent_source_migrates, L"legacy.exe");
    assert_true(migrated_blocked.has_value() &&
                    !migrated_blocked->enabled &&
                    migrated_blocked->origin ==
                        AppInputProfileOrigin::Automatic &&
                    migrated_typing.has_value() &&
                    !migrated_typing->enabled &&
                    migrated_typing->origin ==
                        AppInputProfileOrigin::Automatic,
                "Absent profile source still performs bounded legacy migration");

    const auto invalid_source_migrates =
        vn_ime::ResolveLoadedAppInputProfiles(
            false,
            {{L"untrusted.exe", true, InputMethod::VNI,
              AppInputProfileOrigin::Manual}},
            {L"legacy-only.exe"}, {}, {}, InputMethod::Telex);
    assert_true(!vn_ime::LookupAppInputProfile(
                     invalid_source_migrates, L"untrusted.exe").has_value() &&
                    vn_ime::LookupAppInputProfile(
                        invalid_source_migrates,
                        L"legacy-only.exe").has_value(),
                "Invalid profile source is discarded before legacy migration");

    const auto disabled_profile_is_authoritative =
        vn_ime::PrepareAppInputProfilesForSave(
            {{L"editor.exe", false, InputMethod::VNI,
              AppInputProfileOrigin::Automatic}},
            {}, {}, InputMethod::Telex);
    const auto prepared_disabled = disabled_profile_is_authoritative.has_value()
        ? vn_ime::LookupAppInputProfile(
              *disabled_profile_is_authoritative, L"editor.exe")
        : std::nullopt;
    assert_true(prepared_disabled.has_value() &&
                    !prepared_disabled->enabled &&
                    prepared_disabled->preferred_method == InputMethod::VNI &&
                    prepared_disabled->origin ==
                        AppInputProfileOrigin::Automatic,
                "Disabled new profile survives stale legacy lists with origin intact");

    const auto enabled_profile_is_authoritative =
        vn_ime::PrepareAppInputProfilesForSave(
            {{L"editor.exe", true, InputMethod::SimpleTelex,
              AppInputProfileOrigin::Manual}},
            {L"EDITOR.EXE"}, {L"EDITOR.EXE"}, InputMethod::VNI);
    const auto prepared_enabled = enabled_profile_is_authoritative.has_value()
        ? vn_ime::LookupAppInputProfile(
              *enabled_profile_is_authoritative, L"editor.exe")
        : std::nullopt;
    assert_true(prepared_enabled.has_value() && prepared_enabled->enabled &&
                    prepared_enabled->preferred_method ==
                        InputMethod::SimpleTelex &&
                    prepared_enabled->origin == AppInputProfileOrigin::Manual,
                "Enabled new profile ignores a stale legacy blocked entry");

    const auto legacy_only_prepared =
        vn_ime::PrepareAppInputProfilesForSave(
            {}, {L"EDITOR.EXE", L"manual.exe"}, {L"editor.exe"},
            InputMethod::VNI);
    const auto prepared_legacy = legacy_only_prepared.has_value()
        ? vn_ime::LookupAppInputProfile(
              *legacy_only_prepared, L"editor.exe")
        : std::nullopt;
    assert_true(prepared_legacy.has_value() &&
                    !prepared_legacy->enabled &&
                    prepared_legacy->preferred_method == InputMethod::VNI &&
                    prepared_legacy->origin ==
                        AppInputProfileOrigin::Automatic,
                "Legacy auto-owned block migrates to Automatic Off when profiles are empty");
    const auto prepared_manual_legacy = legacy_only_prepared.has_value()
        ? vn_ime::LookupAppInputProfile(
              *legacy_only_prepared, L"manual.exe")
        : std::nullopt;
    assert_true(prepared_manual_legacy.has_value() &&
                    !prepared_manual_legacy->enabled &&
                    prepared_manual_legacy->origin ==
                        AppInputProfileOrigin::Manual,
                "Legacy block without auto ownership migrates to Manual Off");

    assert_true(!vn_ime::ResolveEnableAppInputProfiles(0, 1, 1),
                "New profile enable setting has highest precedence");
    assert_true(!vn_ime::ResolveEnableAppInputProfiles(std::nullopt, 0, 1),
                "EnableAppBlocklist is the first legacy setting fallback");
    assert_true(vn_ime::ResolveEnableAppInputProfiles(
                    std::nullopt, std::nullopt, 1),
                "EnableAutoExclude is the older legacy setting fallback");
    assert_true(vn_ime::ResolveEnableAppInputProfiles(
                    std::nullopt, std::nullopt, std::nullopt),
                "Missing enable settings retain the enabled default");
    assert_true(!vn_ime::ResolveAppInputProfileSetting(0, 1, true) &&
                    vn_ime::ResolveAppInputProfileSetting(1, 0, false),
                "New per-app setting wins over its legacy fallback");
    assert_true(!vn_ime::ResolveAppInputProfileSetting(99, 0, true) &&
                    vn_ime::ResolveAppInputProfileSetting(
                        std::nullopt, 99, true),
                "Invalid per-app setting values fall back safely");
}

void test_per_app_runtime_and_tray_policy() {
    std::cout << "\nRunning test_per_app_runtime_and_tray_policy..." << std::endl;
    using vn_ime::AppInputMode;
    using vn_ime::AppInputProfileOrigin;
    using vn_ime::AppInputUpdateTarget;
    using vn_ime::TrayClickAction;
    using vn_ime::TrayClickEvent;

    const std::vector<vn_ime::AppInputProfile> effective_profiles = {
        {L"manual.exe", false, InputMethod::VNI,
         AppInputProfileOrigin::Manual},
        {L"automatic.exe", true, InputMethod::SimpleTelex,
         AppInputProfileOrigin::Automatic},
    };
    const auto manual_effective = vn_ime::ResolveEffectiveAppInputProfile(
        true, effective_profiles, L"manual.exe", true,
        InputMethod::Telex);
    const auto automatic_effective = vn_ime::ResolveEffectiveAppInputProfile(
        true, effective_profiles, L"automatic.exe", false,
        InputMethod::VNI);
    const auto profiles_disabled = vn_ime::ResolveEffectiveAppInputProfile(
        false, effective_profiles, L"manual.exe", false,
        InputMethod::Telex);
    const auto inherited_global_english =
        vn_ime::ResolveEffectiveAppInputProfile(
            true, effective_profiles, L"missing.exe", false,
            InputMethod::Telex);
    assert_true(!manual_effective.enabled &&
                    manual_effective.input_method == InputMethod::VNI &&
                    automatic_effective.enabled &&
                    automatic_effective.input_method ==
                        InputMethod::SimpleTelex &&
                    !profiles_disabled.has_explicit_profile &&
                    !profiles_disabled.enabled &&
                    profiles_disabled.input_method == InputMethod::Telex,
                "Runtime resolution honors profiles and global fallback state");
    assert_true(vn_ime::IsExplicitAppInputProfileDisabled(
                    true, manual_effective) &&
                    !vn_ime::IsExplicitAppInputProfileDisabled(
                        true, inherited_global_english) &&
                    !vn_ime::IsExplicitAppInputProfileDisabled(
                        false, manual_effective) &&
                    !vn_ime::IsExplicitAppInputProfileDisabled(
                        true, automatic_effective),
                "Cached blocked state only represents enabled explicit Off profiles");

    vn_ime::IMEConfig automatic_modes;
    automatic_modes.input_method = InputMethod::VNI;
    assert_true(vn_ime::ApplyAutomaticAppInputMode(
                    automatic_modes, L"editor.exe", AppInputMode::Telex),
                "Automatic profile accepts Telex");
    assert_true(vn_ime::ApplyAutomaticAppInputMode(
                    automatic_modes, L"editor.exe",
                    AppInputMode::SimpleTelex),
                "Automatic profile accepts Simple Telex");
    assert_true(vn_ime::ApplyAutomaticAppInputMode(
                    automatic_modes, L"editor.exe", AppInputMode::VNI),
                "Automatic profile accepts VNI");
    assert_true(vn_ime::ApplyAutomaticAppInputMode(
                    automatic_modes, L"editor.exe", AppInputMode::Off),
                "Automatic profile accepts Off");
    const auto automatic_off = vn_ime::LookupAppInputProfile(
        automatic_modes.app_input_profiles, L"editor.exe");
    assert_true(automatic_off.has_value() && !automatic_off->enabled &&
                    automatic_off->preferred_method == InputMethod::VNI &&
                    automatic_off->origin ==
                        AppInputProfileOrigin::Automatic &&
                    vn_ime::ProcessListToText(automatic_modes.blocked_apps) ==
                        L"editor.exe" &&
                    vn_ime::ProcessListToText(
                        automatic_modes.auto_blocked_apps) == L"editor.exe",
                "Automatic Off preserves method and synchronizes legacy views");

    vn_ime::IMEConfig activation_config = automatic_modes;
    assert_true(vn_ime::RestoreAutomaticAppInputProfileOnActivate(
                    activation_config, L"editor.exe"),
                "Activation restores an Automatic Off profile");
    const auto restored_automatic = vn_ime::LookupAppInputProfile(
        activation_config.app_input_profiles, L"editor.exe");
    assert_true(restored_automatic.has_value() &&
                    restored_automatic->enabled &&
                    restored_automatic->preferred_method == InputMethod::VNI &&
                    activation_config.blocked_apps.empty() &&
                    activation_config.auto_blocked_apps.empty(),
                "Automatic activation restore keeps method and clears legacy blocks");
    assert_true(!vn_ime::RestoreAutomaticAppInputProfileOnActivate(
                    activation_config, L"new.exe") &&
                    !vn_ime::LookupAppInputProfile(
                         activation_config.app_input_profiles,
                         L"new.exe").has_value(),
                "Activation never creates a profile for a newly opened app");

    vn_ime::IMEConfig manual_activation;
    manual_activation.app_input_profiles = {
        {L"manual.exe", false, InputMethod::SimpleTelex,
         AppInputProfileOrigin::Manual},
    };
    vn_ime::SyncLegacyAppProfileViews(manual_activation);
    assert_true(!vn_ime::RestoreAutomaticAppInputProfileOnActivate(
                    manual_activation, L"manual.exe"),
                "Activation leaves Manual Off unchanged");
    const auto manual_after_activation = vn_ime::LookupAppInputProfile(
        manual_activation.app_input_profiles, L"manual.exe");
    assert_true(manual_after_activation.has_value() &&
                    !manual_after_activation->enabled &&
                    manual_after_activation->origin ==
                        AppInputProfileOrigin::Manual,
                "Manual Off survives activation");

    assert_true(vn_ime::ShouldLearnAutomaticOffOnDeactivate(
                    true, true, true, true, true, true) &&
                    !vn_ime::ShouldLearnAutomaticOffOnDeactivate(
                        true, true, false, true, true, true) &&
                    !vn_ime::ShouldLearnAutomaticOffOnDeactivate(
                        true, true, true, true, false, true) &&
                    !vn_ime::ShouldLearnAutomaticOffOnDeactivate(
                        true, true, true, true, true, false),
                "Deactivate learning requires activation and exact foreground guards");

    vn_ime::IMEConfig manual_off_learning;
    manual_off_learning.app_input_profiles = {
        {L"manual-off.exe", false, InputMethod::VNI,
         AppInputProfileOrigin::Manual},
    };
    vn_ime::SyncLegacyAppProfileViews(manual_off_learning);
    const auto manual_off_blocked_before = manual_off_learning.blocked_apps;
    const auto manual_off_auto_before = manual_off_learning.auto_blocked_apps;
    assert_true(!vn_ime::LearnAutomaticOffOnDeactivate(
                    manual_off_learning, L"manual-off.exe"),
                "Deactivate does not claim an existing Manual Off profile");
    const auto manual_off_after = vn_ime::LookupAppInputProfile(
        manual_off_learning.app_input_profiles, L"manual-off.exe");
    assert_true(manual_off_after.has_value() &&
                    !manual_off_after->enabled &&
                    manual_off_after->preferred_method == InputMethod::VNI &&
                    manual_off_after->origin == AppInputProfileOrigin::Manual &&
                    manual_off_learning.blocked_apps ==
                        manual_off_blocked_before &&
                    manual_off_learning.auto_blocked_apps ==
                        manual_off_auto_before,
                "Manual Off ownership, method, and legacy views stay unchanged");

    vn_ime::IMEConfig manual_on_learning;
    manual_on_learning.app_input_profiles = {
        {L"manual-on.exe", true, InputMethod::SimpleTelex,
         AppInputProfileOrigin::Manual},
    };
    assert_true(vn_ime::LearnAutomaticOffOnDeactivate(
                    manual_on_learning, L"manual-on.exe"),
                "Explicit switch-away learns Automatic Off from Manual On");
    const auto manual_on_after = vn_ime::LookupAppInputProfile(
        manual_on_learning.app_input_profiles, L"manual-on.exe");
    assert_true(manual_on_after.has_value() && !manual_on_after->enabled &&
                    manual_on_after->preferred_method ==
                        InputMethod::SimpleTelex &&
                    manual_on_after->origin ==
                        AppInputProfileOrigin::Automatic &&
                    vn_ime::ProcessListToText(
                        manual_on_learning.auto_blocked_apps) ==
                        L"manual-on.exe",
                "Learned Manual On profile keeps its preferred method");

    vn_ime::IMEConfig automatic_on_learning;
    automatic_on_learning.app_input_profiles = {
        {L"automatic-on.exe", true, InputMethod::Telex,
         AppInputProfileOrigin::Automatic},
    };
    assert_true(vn_ime::LearnAutomaticOffOnDeactivate(
                    automatic_on_learning, L"automatic-on.exe"),
                "Deactivate learns Off for an existing Automatic On profile");
    const auto automatic_on_after = vn_ime::LookupAppInputProfile(
        automatic_on_learning.app_input_profiles, L"automatic-on.exe");
    assert_true(automatic_on_after.has_value() &&
                    !automatic_on_after->enabled &&
                    automatic_on_after->preferred_method == InputMethod::Telex &&
                    automatic_on_after->origin ==
                        AppInputProfileOrigin::Automatic,
                "Automatic On becomes Automatic Off without losing method");

    vn_ime::IMEConfig new_profile_learning;
    new_profile_learning.input_method = InputMethod::VNI;
    assert_true(vn_ime::LearnAutomaticOffOnDeactivate(
                    new_profile_learning, L"new-auto.exe"),
                "Deactivate creates Automatic Off when no profile exists");
    const auto new_profile_after = vn_ime::LookupAppInputProfile(
        new_profile_learning.app_input_profiles, L"new-auto.exe");
    assert_true(new_profile_after.has_value() &&
                    !new_profile_after->enabled &&
                    new_profile_after->preferred_method == InputMethod::VNI &&
                    new_profile_after->origin ==
                        AppInputProfileOrigin::Automatic,
                "New Automatic Off uses the global preferred method");

    vn_ime::IMEConfig per_app_selection;
    per_app_selection.input_method = InputMethod::VNI;
    per_app_selection.typing_mode = 1;
    const auto selected_for_app = vn_ime::ApplyUserSelectedInputMode(
        per_app_selection, L"code.exe", AppInputMode::Telex);
    const auto selected_profile = vn_ime::LookupAppInputProfile(
        per_app_selection.app_input_profiles, L"code.exe");
    assert_true(selected_for_app.changed &&
                    selected_for_app.target ==
                        AppInputUpdateTarget::AutomaticProfile &&
                    selected_profile.has_value() && selected_profile->enabled &&
                    selected_profile->preferred_method == InputMethod::Telex &&
                    selected_profile->origin ==
                        AppInputProfileOrigin::Automatic &&
                    per_app_selection.input_method == InputMethod::VNI &&
                    per_app_selection.typing_mode == 1,
                "Tray method selection targets current app when auto remember is on");

    const auto toggled_app_off = vn_ime::ToggleUserInputMode(
        per_app_selection, L"code.exe");
    const auto app_after_off = vn_ime::LookupAppInputProfile(
        per_app_selection.app_input_profiles, L"code.exe");
    const auto toggled_app_on = vn_ime::ToggleUserInputMode(
        per_app_selection, L"code.exe");
    const auto app_after_on = vn_ime::LookupAppInputProfile(
        per_app_selection.app_input_profiles, L"code.exe");
    assert_true(toggled_app_off.changed && toggled_app_on.changed &&
                    toggled_app_off.target ==
                        AppInputUpdateTarget::ExistingProfile &&
                    toggled_app_on.target ==
                        AppInputUpdateTarget::ExistingProfile &&
                    app_after_off.has_value() && !app_after_off->enabled &&
                    app_after_off->preferred_method == InputMethod::Telex &&
                    app_after_on.has_value() && app_after_on->enabled &&
                    app_after_on->preferred_method == InputMethod::Telex &&
                    per_app_selection.input_method == InputMethod::VNI &&
                    per_app_selection.typing_mode == 1,
                "Per-app hotkey Off and On preserves the preferred method");

    vn_ime::IMEConfig manual_profile_auto_off;
    manual_profile_auto_off.enable_auto_app_input_profiles = false;
    manual_profile_auto_off.input_method = InputMethod::Telex;
    manual_profile_auto_off.typing_mode = 0;
    manual_profile_auto_off.app_input_profiles = {
        {L"manual-toggle.exe", false, InputMethod::VNI,
         AppInputProfileOrigin::Manual},
    };
    vn_ime::SyncLegacyAppProfileViews(manual_profile_auto_off);
    const auto manual_toggle_on = vn_ime::ToggleUserInputMode(
        manual_profile_auto_off, L"manual-toggle.exe");
    const auto manual_enabled = vn_ime::LookupAppInputProfile(
        manual_profile_auto_off.app_input_profiles, L"manual-toggle.exe");
    assert_true(manual_toggle_on.changed &&
                    manual_toggle_on.target ==
                        AppInputUpdateTarget::ExistingProfile &&
                    manual_enabled.has_value() && manual_enabled->enabled &&
                    manual_enabled->preferred_method == InputMethod::VNI &&
                    manual_enabled->origin == AppInputProfileOrigin::Manual &&
                    manual_profile_auto_off.input_method == InputMethod::Telex &&
                    manual_profile_auto_off.typing_mode == 0,
                "Manual Off toggles On locally when auto remember is disabled");
    const auto manual_select_method = vn_ime::ApplyUserSelectedInputMode(
        manual_profile_auto_off, L"manual-toggle.exe",
        AppInputMode::SimpleTelex);
    const auto manual_method_after = vn_ime::LookupAppInputProfile(
        manual_profile_auto_off.app_input_profiles, L"manual-toggle.exe");
    assert_true(manual_select_method.changed &&
                    manual_select_method.target ==
                        AppInputUpdateTarget::ExistingProfile &&
                    manual_method_after.has_value() &&
                    manual_method_after->enabled &&
                    manual_method_after->preferred_method ==
                        InputMethod::SimpleTelex &&
                    manual_method_after->origin ==
                        AppInputProfileOrigin::Manual &&
                    manual_profile_auto_off.input_method == InputMethod::Telex &&
                    manual_profile_auto_off.typing_mode == 0,
                "Method selection updates an existing rule without global drift");

    vn_ime::IMEConfig global_selection;
    global_selection.enable_auto_app_input_profiles = false;
    global_selection.input_method = InputMethod::VNI;
    global_selection.typing_mode = 1;
    const auto selected_globally = vn_ime::ApplyUserSelectedInputMode(
        global_selection, L"code.exe", AppInputMode::SimpleTelex);
    assert_true(selected_globally.changed &&
                    selected_globally.target == AppInputUpdateTarget::Global &&
                    global_selection.app_input_profiles.empty() &&
                    global_selection.typing_mode == 0 &&
                    global_selection.input_method == InputMethod::SimpleTelex,
                "Auto remember disabled falls back to global method selection");
    const auto toggled_globally = vn_ime::ToggleUserInputMode(
        global_selection, L"code.exe");
    assert_true(toggled_globally.changed &&
                    toggled_globally.target == AppInputUpdateTarget::Global &&
                    global_selection.typing_mode == 1,
                "Hotkey falls back to global typing mode when auto remember is off");

    vn_ime::IMEConfig profiles_disabled_selection;
    profiles_disabled_selection.enable_app_input_profiles = false;
    profiles_disabled_selection.input_method = InputMethod::Telex;
    const auto profiles_disabled_update = vn_ime::ApplyUserSelectedInputMode(
        profiles_disabled_selection, L"code.exe", AppInputMode::VNI);
    assert_true(profiles_disabled_update.changed &&
                    profiles_disabled_update.target ==
                        AppInputUpdateTarget::Global &&
                    profiles_disabled_selection.app_input_profiles.empty() &&
                    profiles_disabled_selection.input_method == InputMethod::VNI,
                "Disabled per-app profiles always target the global mode");

    struct MethodToggleCase {
        InputMethod method;
        const char* name;
    };
    const std::vector<MethodToggleCase> method_toggle_cases = {
        {InputMethod::Telex, "Telex"},
        {InputMethod::SimpleTelex, "Simple Telex"},
        {InputMethod::VNI, "VNI"},
    };
    for (const auto& test_case : method_toggle_cases) {
        vn_ime::IMEConfig global_toggle;
        global_toggle.enable_app_input_profiles = false;
        global_toggle.enable_auto_app_input_profiles = true;
        global_toggle.typing_mode = 0;
        global_toggle.input_method = test_case.method;
        const auto off = vn_ime::ToggleUserInputMode(
            global_toggle, L"editor.exe");
        const auto on = vn_ime::ToggleUserInputMode(
            global_toggle, L"editor.exe");
        assert_true(
            off.changed && on.changed &&
                off.target == AppInputUpdateTarget::Global &&
                on.target == AppInputUpdateTarget::Global &&
                global_toggle.typing_mode == 0 &&
                global_toggle.input_method == test_case.method,
            std::string("Global hotkey Off/On preserves ") +
                test_case.name);

        vn_ime::IMEConfig automatic_toggle;
        automatic_toggle.enable_app_input_profiles = true;
        automatic_toggle.enable_auto_app_input_profiles = true;
        automatic_toggle.typing_mode = 1;
        automatic_toggle.input_method = InputMethod::VNI;
        automatic_toggle.app_input_profiles = {
            {L"editor.exe", true, test_case.method,
             AppInputProfileOrigin::Automatic},
        };
        const DWORD global_typing_mode_before =
            automatic_toggle.typing_mode;
        const InputMethod global_method_before =
            automatic_toggle.input_method;
        const auto app_off = vn_ime::ToggleUserInputMode(
            automatic_toggle, L"editor.exe");
        const auto profile_off = vn_ime::LookupAppInputProfile(
            automatic_toggle.app_input_profiles, L"editor.exe");
        const auto app_on = vn_ime::ToggleUserInputMode(
            automatic_toggle, L"editor.exe");
        const auto profile_on = vn_ime::LookupAppInputProfile(
            automatic_toggle.app_input_profiles, L"editor.exe");
        assert_true(
            app_off.changed && app_on.changed &&
                app_off.target == AppInputUpdateTarget::ExistingProfile &&
                app_on.target == AppInputUpdateTarget::ExistingProfile &&
                profile_off.has_value() && !profile_off->enabled &&
                profile_off->preferred_method == test_case.method &&
                profile_off->origin == AppInputProfileOrigin::Automatic &&
                profile_on.has_value() && profile_on->enabled &&
                profile_on->preferred_method == test_case.method &&
                profile_on->origin == AppInputProfileOrigin::Automatic &&
                automatic_toggle.typing_mode == global_typing_mode_before &&
                automatic_toggle.input_method == global_method_before,
            std::string("Per-app automatic hotkey Off/On preserves ") +
                test_case.name + " without global drift");
    }

    vn_ime::IMEConfig invalid_process_toggle;
    invalid_process_toggle.enable_app_input_profiles = true;
    invalid_process_toggle.enable_auto_app_input_profiles = true;
    invalid_process_toggle.typing_mode = 0;
    invalid_process_toggle.input_method = InputMethod::SimpleTelex;
    const auto invalid_process_result = vn_ime::ToggleUserInputMode(
        invalid_process_toggle, L"");
    assert_true(invalid_process_result.changed &&
                    invalid_process_result.target ==
                        AppInputUpdateTarget::Global &&
                    invalid_process_toggle.typing_mode == 1 &&
                    invalid_process_toggle.input_method ==
                        InputMethod::SimpleTelex,
                "Invalid process hotkey falls back globally without cycling method");

    vn_ime::IMEConfig disabled_profiles_toggle;
    disabled_profiles_toggle.enable_app_input_profiles = false;
    disabled_profiles_toggle.enable_auto_app_input_profiles = true;
    disabled_profiles_toggle.typing_mode = 0;
    disabled_profiles_toggle.input_method = InputMethod::VNI;
    disabled_profiles_toggle.app_input_profiles = {
        {L"editor.exe", false, InputMethod::Telex,
         AppInputProfileOrigin::Manual},
    };
    const auto disabled_profiles_result = vn_ime::ToggleUserInputMode(
        disabled_profiles_toggle, L"editor.exe");
    assert_true(disabled_profiles_result.changed &&
                    disabled_profiles_result.target ==
                        AppInputUpdateTarget::Global &&
                    disabled_profiles_toggle.typing_mode == 1 &&
                    disabled_profiles_toggle.input_method == InputMethod::VNI,
                "Disabled profile feature makes hotkey use global state");

    vn_ime::TrayClickState tray_click;
    assert_true(tray_click.Advance(TrayClickEvent::LeftButtonDown) ==
                    TrayClickAction::ArmSingleClickTimer &&
                    tray_click.single_click_pending,
                "Tray single down arms one delayed toggle");
    assert_true(tray_click.Advance(TrayClickEvent::ForegroundTimer) ==
                    TrayClickAction::None &&
                    tray_click.single_click_pending,
                "Foreground timer does not consume pending tray click");
    assert_true(tray_click.Advance(TrayClickEvent::SingleClickTimer) ==
                    TrayClickAction::ToggleInputMode &&
                    !tray_click.single_click_pending &&
                    tray_click.Advance(TrayClickEvent::SingleClickTimer) ==
                        TrayClickAction::None,
                "Tray single-click timer toggles exactly once");
    assert_true(tray_click.Advance(TrayClickEvent::LeftButtonDown) ==
                    TrayClickAction::ArmSingleClickTimer &&
                    tray_click.Advance(TrayClickEvent::LeftButtonDoubleClick) ==
                        TrayClickAction::CancelSingleClickTimerAndOpenConfig &&
                    !tray_click.single_click_pending &&
                    tray_click.Advance(TrayClickEvent::SingleClickTimer) ==
                        TrayClickAction::None,
                "Tray double click opens config and cancels the toggle");

    vn_ime::TrayClickState timer_failure_click;
    assert_true(timer_failure_click.Advance(TrayClickEvent::LeftButtonDown) ==
                    TrayClickAction::ArmSingleClickTimer &&
                    timer_failure_click.Advance(
                        TrayClickEvent::SingleClickTimerArmFailed) ==
                        TrayClickAction::ToggleInputMode &&
                    !timer_failure_click.single_click_pending &&
                    timer_failure_click.Advance(
                        TrayClickEvent::SingleClickTimerArmFailed) ==
                        TrayClickAction::None,
                "Tray timer failure falls back to exactly one immediate toggle");
}

void test_hotkey_toggle_state() {
    std::cout << "\nRunning test_hotkey_toggle_state..." << std::endl;
    using vn_ime::HotkeyKey;
    using vn_ime::HotkeyMode;
    using vn_ime::HotkeyModifiers;
    using vn_ime::HotkeyToggleState;

    HotkeyToggleState alt_z;
    const HotkeyModifiers alt_only{true, false, false};
    assert_true(alt_z.ShouldClaimTestEvent(
                    HotkeyMode::AltZ, HotkeyKey::Z, true, alt_only) &&
                    !alt_z.control_down && !alt_z.shift_down &&
                    !alt_z.unrelated_key_pressed,
                "Alt+Z TestKeyDown claims without mutating hotkey state");
    int alt_z_toggle_count = 0;
    alt_z_toggle_count += alt_z.DispatchEvent(
        HotkeyMode::AltZ, HotkeyKey::Z, true, false, alt_only);
    alt_z_toggle_count += alt_z.DispatchEvent(
        HotkeyMode::AltZ, HotkeyKey::Z, true, true, alt_only);
    assert_true(alt_z_toggle_count == 1,
                "Alt+Z dispatch toggles once and ignores autorepeat");
    assert_true(!alt_z.ShouldClaimTestEvent(
                    HotkeyMode::AltZ, HotkeyKey::Z, true,
                    HotkeyModifiers{true, true, false}) &&
                    !alt_z.DispatchEvent(
                        HotkeyMode::AltZ, HotkeyKey::Z, true, false,
                        HotkeyModifiers{true, false, true}),
                "Alt+Z rejects extra Ctrl or Shift modifiers");

    const auto run_ctrl_shift = [](HotkeyKey first_release) {
        HotkeyToggleState state;
        const HotkeyKey second_release =
            first_release == HotkeyKey::Control
            ? HotkeyKey::Shift
            : HotkeyKey::Control;
        const bool test_ctrl = state.ShouldClaimTestEvent(
            HotkeyMode::CtrlShift, HotkeyKey::Control, true);
        const bool pristine_after_test = !state.control_down &&
            !state.shift_down && !state.unrelated_key_pressed;
        (void)state.DispatchEvent(
            HotkeyMode::CtrlShift, HotkeyKey::Control, true, false);
        const bool test_shift = state.ShouldClaimTestEvent(
            HotkeyMode::CtrlShift, HotkeyKey::Shift, true);
        (void)state.DispatchEvent(
            HotkeyMode::CtrlShift, HotkeyKey::Shift, true, false);
        const bool test_release = state.ShouldClaimTestEvent(
            HotkeyMode::CtrlShift, first_release, false);
        const bool state_unchanged_by_test = state.control_down &&
            state.shift_down && !state.unrelated_key_pressed;
        const bool first_toggle = state.DispatchEvent(
            HotkeyMode::CtrlShift, first_release, false, false);
        const bool duplicate_toggle = state.DispatchEvent(
            HotkeyMode::CtrlShift, first_release, false, false);
        const bool second_toggle = state.DispatchEvent(
            HotkeyMode::CtrlShift, second_release, false, false);
        return test_ctrl && test_shift && test_release &&
               pristine_after_test && state_unchanged_by_test &&
               first_toggle && !duplicate_toggle && !second_toggle &&
               !state.control_down && !state.shift_down;
    };
    assert_true(run_ctrl_shift(HotkeyKey::Control),
                "Ctrl+Shift toggles once when Control is released first");
    assert_true(run_ctrl_shift(HotkeyKey::Shift),
                "Ctrl+Shift toggles once when Shift is released first");

    HotkeyToggleState canceled_chord;
    (void)canceled_chord.DispatchEvent(
        HotkeyMode::CtrlShift, HotkeyKey::Control, true, false);
    (void)canceled_chord.DispatchEvent(
        HotkeyMode::CtrlShift, HotkeyKey::Shift, true, false);
    const bool unrelated_claimed = canceled_chord.ShouldClaimTestEvent(
        HotkeyMode::CtrlShift, HotkeyKey::Other, true);
    canceled_chord.ObservePassThroughEvent(
        HotkeyMode::CtrlShift, HotkeyKey::Other, true);
    const bool canceled_first = canceled_chord.DispatchEvent(
        HotkeyMode::CtrlShift, HotkeyKey::Shift, false, false);
    const bool canceled_second = canceled_chord.DispatchEvent(
        HotkeyMode::CtrlShift, HotkeyKey::Control, false, false);
    assert_true(!unrelated_claimed && !canceled_first && !canceled_second,
                "An unrelated key passes through and cancels the Ctrl+Shift chord");

    HotkeyToggleState native_shortcut;
    assert_true(native_shortcut.ShouldClaimTestEvent(
                    HotkeyMode::CtrlShift, HotkeyKey::Control, true),
                "Ctrl+Shift tracker observes the Control modifier");
    (void)native_shortcut.DispatchEvent(
        HotkeyMode::CtrlShift, HotkeyKey::Control, true, false);
    const bool paste_claimed = native_shortcut.ShouldClaimTestEvent(
        HotkeyMode::CtrlShift, HotkeyKey::Other, true,
        HotkeyModifiers{false, true, false});
    native_shortcut.ObservePassThroughEvent(
        HotkeyMode::CtrlShift, HotkeyKey::Other, true);
    const bool paste_release_toggled = native_shortcut.DispatchEvent(
        HotkeyMode::CtrlShift, HotkeyKey::Control, false, false);
    assert_true(!paste_claimed && !paste_release_toggled &&
                    !native_shortcut.control_down &&
                    !native_shortcut.unrelated_key_pressed,
                "Ctrl+V passes through without toggling the input mode");

    HotkeyToggleState native_undo;
    (void)native_undo.DispatchEvent(
        HotkeyMode::CtrlShift, HotkeyKey::Control, true, false);
    const bool undo_claimed = native_undo.ShouldClaimTestEvent(
        HotkeyMode::CtrlShift, HotkeyKey::Z, true,
        HotkeyModifiers{false, true, false});
    native_undo.ObservePassThroughEvent(
        HotkeyMode::CtrlShift, HotkeyKey::Z, true);
    assert_true(!undo_claimed &&
                    !native_undo.DispatchEvent(
                        HotkeyMode::CtrlShift, HotkeyKey::Control,
                        false, false),
                "Ctrl+Z passes through without colliding with Alt+Z support");
}

void test_correction_level_config_mapping() {
    std::cout << "\nRunning test_correction_level_config_mapping..." << std::endl;
    vn_ime::IMEConfig config;
    assert_true(config.auto_correct_level == vn_ime::CorrectionLevel::Normal, "Default level is Normal");
    assert_true(config.enable_auto_correct, "Default enable_auto_correct is true");
    assert_true(vn_ime::NormalizeCorrectionLevelValue(0) == vn_ime::CorrectionLevel::Off, "Config level 0 maps to Off");
    assert_true(vn_ime::NormalizeCorrectionLevelValue(1) == vn_ime::CorrectionLevel::Normal, "Config level 1 maps to Normal");
    assert_true(vn_ime::NormalizeCorrectionLevelValue(2) == vn_ime::CorrectionLevel::Advanced, "Config level 2 maps to Advanced");
    assert_true(vn_ime::NormalizeCorrectionLevelValue(3) == vn_ime::CorrectionLevel::Experimental, "Config level 3 maps to Experimental");
    assert_true(vn_ime::NormalizeCorrectionLevelValue(99) == vn_ime::CorrectionLevel::Normal, "Invalid config level falls back to Normal");
    assert_true(vn_ime::CorrectionLevelToConfigIndex(vn_ime::CorrectionLevel::Advanced) == 2, "Advanced combo index is valid");
    assert_true(vn_ime::CorrectionLevelToConfigIndex(static_cast<vn_ime::CorrectionLevel>(99)) == 1, "Invalid combo index falls back to Normal");

    assert_true(config.english_protection_level == EnglishProtectionLevel::Balanced,
                "Default English protection level is Balanced");
    assert_true(vn_ime::NormalizeEnglishProtectionLevelValue(0) == EnglishProtectionLevel::Off,
                "English protection level 0 maps to Off");
    assert_true(vn_ime::NormalizeEnglishProtectionLevelValue(1) == EnglishProtectionLevel::Balanced,
                "English protection level 1 maps to Balanced");
    assert_true(vn_ime::NormalizeEnglishProtectionLevelValue(2) == EnglishProtectionLevel::EnglishFirst,
                "English protection level 2 maps to English First");
    assert_true(vn_ime::NormalizeEnglishProtectionLevelValue(99) == EnglishProtectionLevel::Balanced,
                "Invalid English protection level falls back to Balanced");
    assert_true(vn_ime::ResolveEnglishProtectionLevel(std::nullopt, std::nullopt) == EnglishProtectionLevel::Balanced,
                "Missing English protection values migrate to Balanced");
    assert_true(vn_ime::ResolveEnglishProtectionLevel(std::nullopt, 0) == EnglishProtectionLevel::Off,
                "Legacy disabled English protection migrates to Off");
    assert_true(vn_ime::ResolveEnglishProtectionLevel(std::nullopt, 1) == EnglishProtectionLevel::Balanced,
                "Legacy enabled English protection migrates to Balanced");
    assert_true(vn_ime::ResolveEnglishProtectionLevel(2, 0) == EnglishProtectionLevel::EnglishFirst,
                "New English protection level wins over legacy bool");
    assert_true(vn_ime::EnglishProtectionLevelToConfigIndex(EnglishProtectionLevel::EnglishFirst) == 2,
                "English First round-trips through combo index");
    assert_true(vn_ime::EnglishProtectionLevelToConfigIndex(static_cast<EnglishProtectionLevel>(99)) == 1,
                "Invalid English protection combo index falls back to Balanced");

    assert_true(config.enable_smart_undo,
                "Smart Undo defaults to enabled");
    assert_true(vn_ime::ResolveSmartUndoEnabled(std::nullopt),
                "Missing Smart Undo registry value preserves enabled default");
    assert_true(!vn_ime::ResolveSmartUndoEnabled(0),
                "Smart Undo registry zero disables the feature");
    assert_true(vn_ime::ResolveSmartUndoEnabled(1),
                "Smart Undo registry one enables the feature");
    assert_true(vn_ime::ResolveSmartUndoEnabled(99),
                "Smart Undo normalizes nonzero registry values to enabled");
    assert_true(vn_ime::SmartUndoEnabledToRegistryValue(false) == 0 &&
                    vn_ime::SmartUndoEnabledToRegistryValue(true) == 1,
                "Smart Undo save helper emits canonical DWORD booleans");

    assert_true(config.enable_smart_context_protection,
                "Smart context protection defaults to enabled");
    assert_true(vn_ime::ResolveSmartContextProtectionEnabled(std::nullopt),
                "Missing smart context registry value preserves enabled default");
    assert_true(!vn_ime::ResolveSmartContextProtectionEnabled(0),
                "Smart context registry zero disables the feature");
    assert_true(vn_ime::ResolveSmartContextProtectionEnabled(1) &&
                    vn_ime::ResolveSmartContextProtectionEnabled(99),
                "Smart context registry values normalize to canonical bool");
    assert_true(
        vn_ime::SmartContextProtectionEnabledToRegistryValue(false) == 0 &&
            vn_ime::SmartContextProtectionEnabledToRegistryValue(true) == 1,
        "Smart context save helper emits canonical DWORD booleans");
    assert_true(
        !config.enable_auto_word_segmentation &&
            !vn_ime::ResolveAutoWordSegmentationEnabled(std::nullopt) &&
            !vn_ime::ResolveAutoWordSegmentationEnabled(0) &&
            vn_ime::ResolveAutoWordSegmentationEnabled(1) &&
            vn_ime::ResolveAutoWordSegmentationEnabled(99),
        "Auto word segmentation defaults Off and normalizes registry values");
    assert_true(
        vn_ime::AutoWordSegmentationEnabledToRegistryValue(false) == 0 &&
            vn_ime::AutoWordSegmentationEnabledToRegistryValue(true) == 1,
        "Auto word segmentation save helper emits canonical DWORD booleans");
    assert_true(
        vn_ime::IsAutoWordSegmentationAvailable(
            CorrectionLevel::Experimental) &&
            !vn_ime::IsAutoWordSegmentationAvailable(
                CorrectionLevel::Advanced) &&
            !vn_ime::NormalizeAutoWordSegmentationEnabled(
                true, CorrectionLevel::Normal) &&
            vn_ime::NormalizeAutoWordSegmentationEnabled(
                true, CorrectionLevel::Experimental) &&
            !vn_ime::NormalizeAutoWordSegmentationEnabled(
                false, CorrectionLevel::Experimental),
        "Auto word segmentation is available only at Experimental level");
}

void test_smart_context_protection() {
    std::cout << "\nRunning test_smart_context_protection..." << std::endl;

    // The policy is deliberately narrower than "letters plus digits".
    assert_true(ClassifySmartContextToken(L"toan@") ==
                    SmartContextKind::Email,
                "Email marker starts protected context");
    assert_true(ClassifySmartContextToken(L"www.") ==
                    SmartContextKind::Url &&
                    ClassifySmartContextToken(L"https:") ==
                    SmartContextKind::Url,
                "Explicit www/http prefixes start URL context");
    assert_true(ClassifySmartContextToken(L"user_name") ==
                    SmartContextKind::Code,
                "Underscore identifier is code context");
    assert_true(ClassifySmartContextToken(L"CamelCase") ==
                    SmartContextKind::Code &&
                    ClassifySmartContextToken(L"camelCase") ==
                    SmartContextKind::Code,
                "Internal lower-to-upper transition is CamelCase code");
    assert_true(ClassifySmartContextToken(L"Hello") ==
                    SmartContextKind::None,
                "Leading TitleCase alone is not code");
    assert_true(ClassifySmartContextToken(L"base64") ==
                    SmartContextKind::Code &&
                    ClassifySmartContextToken(L"sha256") ==
                    SmartContextKind::Code &&
                    ClassifySmartContextToken(L"utf8") ==
                    SmartContextKind::Code &&
                    ClassifySmartContextToken(L"windows11") ==
                    SmartContextKind::Code,
                "Known code families with digits are protected");
    assert_true(ClassifySmartContextToken(L"abc123") ==
                    SmartContextKind::None &&
                    ClassifySmartContextToken(L"a1") ==
                    SmartContextKind::None &&
                    ClassifySmartContextToken(L"e6") ==
                    SmartContextKind::None &&
                    ClassifySmartContextToken(L"tuyen61") ==
                    SmartContextKind::None,
                "Arbitrary alphanumeric and canonical VNI sequences are not code");

    assert_true(ShouldContinueSmartContextToken(L"toan", L'@') &&
                    ShouldContinueSmartContextToken(L"www", L'.') &&
                    ShouldContinueSmartContextToken(L"https", L':') &&
                    ShouldContinueSmartContextToken(L"https:", L'/') &&
                    ShouldContinueSmartContextToken(L"user", L'_') &&
                    ShouldContinueSmartContextToken(L"base", L'6'),
                "Explicit markers and known code family cross composition boundaries");
    assert_true(!ShouldContinueSmartContextToken(L"xin", L'.') &&
                    !ShouldContinueSmartContextToken(L"abc", L'1') &&
                    !ShouldContinueSmartContextToken(L"word", L',') &&
                    !ShouldContinueSmartContextToken(L"word", L' ') &&
                    !ShouldContinueSmartContextToken(L"word", L'\n'),
                "Ordinary punctuation, boundaries, and broad alphanumeric stay native");

    std::wstring active_url = L"https:";
    for (const wchar_t ch : std::wstring_view(L"//a.b/p?q=x&n=1")) {
        assert_true(ShouldContinueSmartContextToken(active_url, ch),
                    "Active URL accepts bounded path and query character");
        active_url.push_back(ch);
    }
    assert_true(ClassifySmartContextToken(active_url) ==
                    SmartContextKind::Url,
                "URL path and query remain in URL context");

    std::wstring active_email = L"toan@gmail";
    for (const wchar_t ch : std::wstring_view(L".com")) {
        assert_true(ShouldContinueSmartContextToken(active_email, ch),
                    "Active email accepts domain dot and suffix");
        active_email.push_back(ch);
    }
    assert_true(ClassifySmartContextToken(active_email) ==
                    SmartContextKind::Email,
                "Email domain dot remains in email context");
    assert_true(!ShouldContinueSmartContextToken(active_url, L')') &&
                    !ShouldContinueSmartContextToken(active_url, L' ') &&
                    !ShouldContinueSmartContextToken(active_url, L'\n') &&
                    !ShouldContinueSmartContextToken(active_email, L')') &&
                    !ShouldContinueSmartContextToken(active_email, L' ') &&
                    !ShouldContinueSmartContextToken(active_email, L'\n'),
                "Invalid smart-context characters return to native boundary handling");

    constexpr InputMethod methods[] = {
        InputMethod::Telex,
        InputMethod::SimpleTelex,
        InputMethod::VNI,
    };
    constexpr std::wstring_view protected_tokens[] = {
        L"toan@gmail.com",
        L"www.example.com",
        L"https://example.com/path",
        L"user_name",
        L"CamelCase",
        L"base64",
        L"sha256",
        L"utf8",
        L"windows11",
    };

    for (const InputMethod method : methods) {
        for (const std::wstring_view token : protected_tokens) {
            Engine engine(method);
            engine.SetEnglishProtectionLevel(EnglishProtectionLevel::Off);
            engine.SetSmartContextProtection(true);
            type_string(engine, token);
            assert_eq(
                engine.GetDisplayString(), std::wstring(token),
                "Smart context preserves raw across input method");
            engine.SecureClear();
        }
    }

    Engine late_email(InputMethod::Telex);
    late_email.SetEnglishProtectionLevel(EnglishProtectionLevel::Off);
    type_string(late_email, L"max");
    assert_eq(late_email.GetDisplayString(), L"m\u00E3",
              "Before email marker normal Telex conversion remains active");
    late_email.ProcessKey(L'@');
    assert_eq(late_email.GetDisplayString(), L"max@",
              "Late email marker restores the entire raw token");
    assert_eq(late_email.GetRawString(), L"max@",
              "Late email marker preserves exact raw keys");
    assert_true(late_email.BackspaceDisplayChar(),
                "BackspaceDisplayChar removes late email marker");
    assert_eq(late_email.GetRawString(), L"max",
              "BackspaceDisplayChar reconstructs raw before email marker");
    assert_eq(late_email.GetDisplayString(), L"m\u00E3",
              "BackspaceDisplayChar resumes normal Telex before marker");
    assert_true(late_email.ShouldContinueSmartContext(L'@'),
                "Removed email marker can be routed again");
    late_email.ProcessKey(L'@');
    assert_eq(late_email.GetDisplayString(), L"max@",
              "Retyping email marker restores raw again");

    Engine late_identifier(InputMethod::Telex);
    late_identifier.SetEnglishProtectionLevel(EnglishProtectionLevel::Off);
    type_string(late_identifier, L"maxV");
    assert_eq(late_identifier.GetDisplayString(), L"maxV",
              "Late CamelCase transition restores the entire raw token");

    Engine disabled(InputMethod::Telex);
    disabled.SetEnglishProtectionLevel(EnglishProtectionLevel::Off);
    disabled.SetSmartContextProtection(false);
    type_string(disabled, L"max");
    assert_eq(disabled.GetDisplayString(), L"m\u00E3",
              "Disabled smart context keeps observable legacy Telex output");
    assert_true(!disabled.ShouldContinueSmartContext(L'@') &&
                    !disabled.ShouldContinueSmartContext(L'_') &&
                    !disabled.ShouldContinueSmartContext(L'.') &&
                    !disabled.ShouldContinueSmartContext(L':'),
                "Disabled option never routes smart context markers");
    disabled.Clear();
    type_string(disabled, L"as");
    assert_eq(disabled.GetDisplayString(), L"\u00E1",
              "Disabled smart context keeps later Telex behavior unchanged");

    Engine disabled_vni(InputMethod::VNI);
    disabled_vni.SetEnglishProtectionLevel(EnglishProtectionLevel::Off);
    disabled_vni.SetSmartContextProtection(false);
    type_string(disabled_vni, L"windows11");
    assert_eq(disabled_vni.GetDisplayString(), L"windows1",
              "Disabled smart context restores legacy VNI code-digit behavior");
    disabled_vni.Clear();
    type_string(disabled_vni, L"base");
    assert_true(!disabled_vni.ShouldContinueSmartContext(L'6'),
                "Disabled smart context does not route known code-family digits");

    const auto verify_display_backspace = [](
        InputMethod method,
        std::wstring_view token,
        std::string_view label) {
        Engine engine(method);
        engine.SetEnglishProtectionLevel(EnglishProtectionLevel::Off);
        engine.SetSmartContextProtection(true);
        type_string(engine, token);
        assert_eq(engine.GetRawString(), std::wstring(token),
                  std::string(label) + " starts with exact raw token");
        assert_eq(engine.GetDisplayString(), std::wstring(token),
                  std::string(label) + " starts with exact literal display");
        assert_true(engine.BackspaceDisplayChar(),
                    std::string(label) + " display backspace succeeds");
        const std::wstring expected_after(token.substr(0, token.length() - 1));
        assert_eq(engine.GetRawString(), expected_after,
                  std::string(label) + " display backspace preserves raw prefix");
        assert_eq(engine.GetDisplayString(), expected_after,
                  std::string(label) + " display backspace preserves literal prefix");
        engine.ProcessKey(token.back());
        assert_eq(engine.GetRawString(), std::wstring(token),
                  std::string(label) + " retype restores exact raw token");
        assert_eq(engine.GetDisplayString(), std::wstring(token),
                  std::string(label) + " retype restores exact literal display");
        engine.SecureClear();
    };
    verify_display_backspace(
        InputMethod::Telex, L"toan@gmail.com", "Email");
    verify_display_backspace(
        InputMethod::SimpleTelex, L"https://a.b/p?q=x&n=1", "URL");
    verify_display_backspace(
        InputMethod::VNI, L"base64", "Known code family");
    verify_display_backspace(
        InputMethod::Telex, L"user_name", "Underscore identifier");

    struct VietnameseControl {
        std::wstring_view keys;
        std::wstring_view expected;
    };
    constexpr VietnameseControl vni_controls[] = {
        {L"a1", L"\u00E1"},
        {L"e6", L"\u00EA"},
        {L"o6", L"\u00F4"},
        {L"u7", L"\u01B0"},
        {L"a8", L"\u0103"},
        {L"tuyen61", L"tuy\u1EBFn"},
    };
    for (const VietnameseControl& control : vni_controls) {
        Engine engine(InputMethod::VNI);
        engine.SetEnglishProtectionLevel(EnglishProtectionLevel::Off);
        engine.SetSmartContextProtection(true);
        type_string(engine, control.keys);
        assert_eq(engine.GetDisplayString(), std::wstring(control.expected),
                  "Smart context keeps canonical VNI conversion");
    }

    Engine telex(InputMethod::Telex);
    telex.SetEnglishProtectionLevel(EnglishProtectionLevel::Off);
    type_string(telex, L"tes");
    assert_eq(telex.GetDisplayString(), L"t\u00E9",
              "Smart context keeps canonical Telex tone conversion");
    telex.Clear();
    type_string(telex, L"tee");
    assert_eq(telex.GetDisplayString(), L"t\u00EA",
              "Smart context keeps canonical Telex shape conversion");

    Engine boundary(InputMethod::Telex);
    boundary.SetEnglishProtectionLevel(EnglishProtectionLevel::Off);
    type_string(boundary, L"toan@gmail.com");
    assert_eq(boundary.GetDisplayString(), L"toan@gmail.com",
              "Protected context remains literal until a native boundary");
    boundary.Clear();
    type_string(boundary, L"as");
    assert_eq(boundary.GetDisplayString(), L"\u00E1",
              "Boundary reset does not leak protection into the next word");

    Engine clear_reset(InputMethod::Telex);
    clear_reset.SetEnglishProtectionLevel(EnglishProtectionLevel::Off);
    type_string(clear_reset, L"https://a.b/p?q=x&n=1");
    clear_reset.Clear();
    assert_true(clear_reset.GetRawString().empty() &&
                    clear_reset.GetDisplayString().empty(),
                "Clear removes all smart-context state");
    type_string(clear_reset, L"tes");
    assert_eq(clear_reset.GetDisplayString(), L"t\u00E9",
              "Clear is followed by normal Telex conversion");

    Engine secure_clear_reset(InputMethod::VNI);
    secure_clear_reset.SetEnglishProtectionLevel(
        EnglishProtectionLevel::Off);
    type_string(secure_clear_reset, L"toan@gmail.com");
    secure_clear_reset.SecureClear();
    assert_true(secure_clear_reset.GetRawString().empty() &&
                    secure_clear_reset.GetDisplayString().empty(),
                "SecureClear removes all smart-context state");
    type_string(secure_clear_reset, L"tuyen61");
    assert_eq(secure_clear_reset.GetDisplayString(), L"tuy\u1EBFn",
              "SecureClear is followed by normal VNI conversion");

    const std::wstring oversized(
        kMaxRawKeysPerComposition + 1, L'a');
    assert_true(ClassifySmartContextToken(oversized) ==
                    SmartContextKind::None,
                "Smart context rejects oversized tokens");
    const std::wstring at_limit(kMaxRawKeysPerComposition, L'a');
    assert_true(!ShouldContinueSmartContextToken(at_limit, L'@'),
                "Smart context continuation is bounded at composition limit");

    constexpr size_t iterations = 50000;
    size_t classified = 0;
    const auto start = std::chrono::steady_clock::now();
    for (size_t i = 0; i < iterations; ++i) {
        classified += ClassifySmartContextToken(L"https://example.com/path") ==
            SmartContextKind::Url;
    }
    const double average_us = std::chrono::duration<double, std::micro>(
        std::chrono::steady_clock::now() - start).count() /
        static_cast<double>(iterations);
    std::cout << "  Smart context classifier average: "
              << average_us << " us/call" << std::endl;
    assert_true(classified == iterations,
                "Smart context latency loop executes every decision");
    assert_true(average_us < 20.0,
                "Smart context classifier stays under broad latency guard");
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
    assert_true(
        parsed.limit_exceeded_lines == 0,
        "Ordinary shorthand rules do not trip resource limits");

    const std::wstring overlong_key(
        vn_ime::MAX_SHORTHAND_KEY_CHARS + 1, L'k');
    const vn_ime::ShorthandParseResult overlong =
        vn_ime::ParseShorthandRules(overlong_key + L"=value\n");
    assert_true(
        overlong.rules.empty() && overlong.invalid_lines == 1 &&
            overlong.limit_exceeded_lines == 1,
        "Shorthand parser rejects overlong keys without retaining them");

    std::wstring bounded_rules;
    bounded_rules.reserve(vn_ime::MAX_SHORTHAND_RULES * 12);
    for (size_t index = 0;
         index < vn_ime::MAX_SHORTHAND_RULES + 1; ++index) {
        bounded_rules += L"k" + std::to_wstring(index) + L"=v\n";
    }
    const auto parse_start = std::chrono::steady_clock::now();
    const vn_ime::ShorthandParseResult bounded =
        vn_ime::ParseShorthandRules(bounded_rules);
    const auto parse_elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - parse_start);
    assert_true(
        bounded.rules.size() == vn_ime::MAX_SHORTHAND_RULES &&
            bounded.invalid_lines == 1 &&
            bounded.limit_exceeded_lines == 1,
        "Shorthand parser caps the number of retained rules");
    assert_true(
        parse_elapsed.count() < 2000,
        "Bounded shorthand table parses without quadratic slowdown");

    assert_eq(
        vn_ime::BuildUserShorthandFilePath(L"C:\\Users\\Test\\AppData\\Local"),
        L"C:\\Users\\Test\\AppData\\Local\\Neokey\\neokey_shorthand.txt",
        "Shorthand path uses per-user LocalAppData");
    assert_eq(
        vn_ime::BuildUserShorthandFilePath(L"C:\\Users\\Test\\AppData\\Local\\"),
        L"C:\\Users\\Test\\AppData\\Local\\Neokey\\neokey_shorthand.txt",
        "Shorthand path handles a trailing separator");
    assert_eq(
        vn_ime::BuildUserShorthandFilePath(L""),
        L"",
        "Shorthand path rejects a missing LocalAppData root");
}

void test_dynamic_shorthand_templates() {
    std::cout << "\nRunning test_dynamic_shorthand_templates..."
              << std::endl;

    const auto formatted_date =
        vn_ime::FormatShorthandDate(30, 8, 2026);
    assert_true(
        formatted_date && *formatted_date == L"30/08/2026",
        "Dynamic shorthand formats local date as DD/MM/YYYY");
    assert_true(
        !vn_ime::FormatShorthandDate(0, 8, 2026) &&
            !vn_ime::FormatShorthandDate(30, 13, 2026),
        "Dynamic shorthand date formatter fails closed on invalid fields");
    const auto formatted_time = vn_ime::FormatShorthandTime(7, 5);
    assert_true(
        formatted_time && *formatted_time == L"07:05" &&
            !vn_ime::FormatShorthandTime(24, 0) &&
            !vn_ime::FormatShorthandTime(23, 60),
        "Dynamic shorthand formats bounded local time as HH:mm");
    const auto weekday = vn_ime::FormatShorthandWeekday(0);
    assert_true(
        weekday && *weekday == L"Ch\u1EE7 nh\u1EADt" &&
            !vn_ime::FormatShorthandWeekday(7),
        "Dynamic shorthand maps Windows weekday values to Vietnamese");

    const std::wstring date =
        formatted_date.value_or(L"30/08/2026");
    const std::wstring clipboard =
        L"Nguy\u1EC5n V\u0103n A\r\nD\u00F2ng 2";
    vn_ime::DynamicShorthandValues values;
    values.date = std::wstring_view(date);
    const std::wstring time = formatted_time.value_or(L"07:05");
    values.time = std::wstring_view(time);
    values.weekday = weekday.value_or(L"Ch\u1EE7 nh\u1EADt");
    const std::wstring uuid = L"12345678-1234-4abc-8def-1234567890ab";
    values.uuid = std::wstring_view(uuid);
    values.clipboard = std::wstring_view(clipboard);

    const auto static_result =
        vn_ime::ResolveDynamicShorthandTemplate(
            L"Vi\u1EC7t Nam", {}, vn_ime::MAX_SHORTHAND_VALUE_CHARS);
    assert_true(
        static_result && *static_result == L"Vi\u1EC7t Nam",
        "Static shorthand remains backward compatible");

    const auto date_result =
        vn_ime::ResolveDynamicShorthandTemplate(
            L"H\u00F4m nay l\u00E0 ng\u00E0y {{DD/MM/YYYY}}", values,
            vn_ime::MAX_SHORTHAND_VALUE_CHARS);
    assert_true(
        date_result &&
            *date_result == L"H\u00F4m nay l\u00E0 ng\u00E0y 30/08/2026",
        "Dynamic shorthand resolves the date tag");

    const auto utility_result =
        vn_ime::ResolveDynamicShorthandTemplate(
            L"{{DATE}} {{TIME}} {{WEEKDAY}} {{UUID}}{{NEWLINE}}A{{TAB}}B",
            values, vn_ime::MAX_SHORTHAND_VALUE_CHARS);
    assert_true(
        utility_result && *utility_result ==
            L"30/08/2026 07:05 Ch\u1EE7 nh\u1EADt "
            L"12345678-1234-4abc-8def-1234567890ab\r\nA\tB",
        "Dynamic shorthand resolves date, time, weekday, UUID, newline, and tab");

    const auto cursor_result =
        vn_ime::ResolveDynamicShorthandTemplateWithSelection(
            L"tr\u01B0\u1EDBc {{CURSOR}} sau", values,
            vn_ime::MAX_SHORTHAND_VALUE_CHARS);
    assert_true(
        cursor_result && cursor_result->text == L"tr\u01B0\u1EDBc  sau" &&
            cursor_result->selection_start == 6 &&
            cursor_result->selection_end == 6,
        "CURSOR is removed and carries an exact relative caret");
    assert_true(
        !vn_ime::ResolveDynamicShorthandTemplateWithSelection(
            L"{{CURSOR}}a{{CURSOR}}", values,
            vn_ime::MAX_SHORTHAND_VALUE_CHARS),
        "Multiple CURSOR markers fail closed instead of choosing ambiguously");

    assert_eq(
        vn_ime::TrimShorthandText(
            L"\u00A0\t N\u1ED9i dung \r\n\u3000"),
        L"N\u1ED9i dung",
        "Clipboard trim removes bounded Unicode edge whitespace");
    const std::wstring selected_text = L"\u0111o\u1EA1n \u0111ang ch\u1ECDn";
    const std::wstring clipboard_trimmed = L"MiXeD@example.com";
    const std::wstring clipboard_upper = L"MIXED@EXAMPLE.COM";
    const std::wstring clipboard_lower = L"mixed@example.com";
    vn_ime::DynamicShorthandValues transform_values;
    transform_values.selection = std::wstring_view(selected_text);
    transform_values.clipboard = std::wstring_view(clipboard_trimmed);
    transform_values.clipboard_trim = std::wstring_view(clipboard_trimmed);
    transform_values.clipboard_upper = std::wstring_view(clipboard_upper);
    transform_values.clipboard_lower = std::wstring_view(clipboard_lower);
    const auto transform_result =
        vn_ime::ResolveDynamicShorthandTemplateWithSelection(
            L"[{{SELECTION}}] {{CLIPBOARD|TRIM}} | "
            L"{{CLIPBOARD|UPPER}} | {{CLIPBOARD|LOWER}}{{CURSOR}}",
            transform_values, vn_ime::MAX_SHORTHAND_VALUE_CHARS);
    const std::wstring expected_transform =
        L"[\u0111o\u1EA1n \u0111ang ch\u1ECDn] MiXeD@example.com | "
        L"MIXED@EXAMPLE.COM | mixed@example.com";
    assert_true(
        transform_result && transform_result->text == expected_transform &&
            transform_result->selection_start == expected_transform.length() &&
            transform_result->selection_end == expected_transform.length(),
        "Selection and bounded clipboard transforms compose with CURSOR");
    assert_true(
        !vn_ime::ResolveDynamicShorthandTemplateWithSelection(
            L"{{SELECTION}}", {}, vn_ime::MAX_SHORTHAND_VALUE_CHARS),
        "Missing captured selection fails closed without partial output");
    assert_true(
        vn_ime::PlanShorthandSelectionCapture(true, false) ==
            vn_ime::ShorthandSelectionCapturePlan::Capture,
        "Selection shorthand captures before its first physical key");
    assert_true(
        vn_ime::PlanShorthandSelectionCapture(true, true) ==
            vn_ime::ShorthandSelectionCapturePlan::Preserve,
        "Repeated key testing preserves an already captured selection");
    assert_true(
        vn_ime::PlanShorthandSelectionCapture(false, true) ==
            vn_ime::ShorthandSelectionCapturePlan::Clear,
        "A different shortcut prefix clears stale captured selection");

    const auto clipboard_result =
        vn_ime::ResolveDynamicShorthandTemplate(
            L"K\u00EDnh g\u1EEDi {{CLIPBOARD}},\r\nng\u00E0y {{DD/MM/YYYY}}",
            values, vn_ime::MAX_SHORTHAND_VALUE_CHARS);
    assert_true(
        clipboard_result &&
            *clipboard_result ==
                L"K\u00EDnh g\u1EEDi Nguy\u1EC5n V\u0103n A\r\nD\u00F2ng 2,\r\n"
                L"ng\u00E0y 30/08/2026",
        "Dynamic shorthand preserves Unicode and multiline clipboard text");

    const auto repeated_result =
        vn_ime::ResolveDynamicShorthandTemplate(
            L"{{DD/MM/YYYY}} | {{DD/MM/YYYY}} | {{CLIPBOARD}}",
            values, vn_ime::MAX_SHORTHAND_VALUE_CHARS);
    assert_true(
        repeated_result &&
            *repeated_result ==
                L"30/08/2026 | 30/08/2026 | Nguy\u1EC5n V\u0103n A\r\nD\u00F2ng 2",
        "Dynamic shorthand resolves repeated known tags in one pass");

    const auto unknown_result =
        vn_ime::ResolveDynamicShorthandTemplate(
            L"Gi\u1EEF nguy\u00EAn {{UNKNOWN}}", values,
            vn_ime::MAX_SHORTHAND_VALUE_CHARS);
    assert_true(
        unknown_result && *unknown_result == L"Gi\u1EEF nguy\u00EAn {{UNKNOWN}}",
        "Unknown shorthand tags remain literal");

    vn_ime::DynamicShorthandValues missing_clipboard;
    missing_clipboard.date = std::wstring_view(date);
    assert_true(
        !vn_ime::ResolveDynamicShorthandTemplate(
            L"{{CLIPBOARD}}", missing_clipboard,
            vn_ime::MAX_SHORTHAND_VALUE_CHARS),
        "Missing clipboard data fails closed without partial expansion");

    vn_ime::DynamicShorthandValues missing_date;
    missing_date.clipboard = std::wstring_view(clipboard);
    assert_true(
        !vn_ime::ResolveDynamicShorthandTemplate(
            L"{{DD/MM/YYYY}}", missing_date,
            vn_ime::MAX_SHORTHAND_VALUE_CHARS),
        "Missing date data fails closed without partial expansion");
    assert_true(
        !vn_ime::ResolveDynamicShorthandTemplate(
            L"{{CLIPBOARD}} / {{DD/MM/YYYY}}", missing_date,
            vn_ime::MAX_SHORTHAND_VALUE_CHARS),
        "A later missing provider rejects the whole dynamic expansion");

    const std::wstring mixed_case_clipboard = L"MiXeD@example.com";
    vn_ime::DynamicShorthandValues casing_values;
    casing_values.clipboard = std::wstring_view(mixed_case_clipboard);
    const auto casing_result =
        vn_ime::ResolveDynamicShorthandTemplate(
            L"EMAIL: {{CLIPBOARD}}", casing_values,
            vn_ime::MAX_SHORTHAND_VALUE_CHARS);
    assert_true(
        casing_result &&
            *casing_result == L"EMAIL: MiXeD@example.com",
        "Template casing does not alter clipboard casing");

    const std::wstring short_clipboard = L"0123456789ABCDEF";
    vn_ime::DynamicShorthandValues bounded_values;
    bounded_values.clipboard = std::wstring_view(short_clipboard);
    const std::wstring exact_template(
        vn_ime::MAX_SHORTHAND_VALUE_CHARS - short_clipboard.length(),
        L'a');
    const auto exact_limit =
        vn_ime::ResolveDynamicShorthandTemplate(
            exact_template + std::wstring(vn_ime::SHORTHAND_CLIPBOARD_TAG),
            bounded_values, vn_ime::MAX_SHORTHAND_VALUE_CHARS);
    assert_true(
        exact_limit &&
            exact_limit->length() == vn_ime::MAX_SHORTHAND_VALUE_CHARS,
        "Dynamic shorthand accepts output exactly at the size limit");

    const std::wstring over_template(
        vn_ime::MAX_SHORTHAND_VALUE_CHARS - short_clipboard.length() + 1,
        L'a');
    assert_true(
        !vn_ime::ResolveDynamicShorthandTemplate(
            over_template + std::wstring(vn_ime::SHORTHAND_CLIPBOARD_TAG),
            bounded_values, vn_ime::MAX_SHORTHAND_VALUE_CHARS),
        "Dynamic shorthand rejects output above the size limit");

    const std::wstring large_clipboard(
        vn_ime::MAX_SHORTHAND_VALUE_CHARS / 2 + 1, L'x');
    vn_ime::DynamicShorthandValues repeated_values;
    repeated_values.clipboard = std::wstring_view(large_clipboard);
    assert_true(
        !vn_ime::ResolveDynamicShorthandTemplate(
            L"{{CLIPBOARD}}{{CLIPBOARD}}", repeated_values,
            vn_ime::MAX_SHORTHAND_VALUE_CHARS),
        "Repeated clipboard tags cannot bypass the output limit");

    const vn_ime::ShorthandParseResult parsed =
        vn_ime::ParseShorthandRules(
            L"dday=H\u00F4m nay l\u00E0 ng\u00E0y {{DD/MM/YYYY}}\n"
            L"xchao=K\u00EDnh g\u1EEDi {{CLIPBOARD}},\n"
            L"wrap=[{{SELECTION}}]{{CURSOR}}\n"
            L"clip={{CLIPBOARD|TRIM}}\n");
    assert_true(
        parsed.rules.size() == 4 && parsed.invalid_lines == 0,
        "Shorthand parser accepts dynamic tags without a format change");
}

void test_shorthand_reload_policy() {
    std::cout << "\nRunning test_shorthand_reload_policy..." << std::endl;

    const vn_ime::ShorthandFileVersion missing{};
    const vn_ime::ShorthandFileVersion first{
        true, 100, 0};
    const vn_ime::ShorthandFileVersion same{
        true, 100, 0};
    const vn_ime::ShorthandFileVersion changed_time{
        true, 101, 0};
    const vn_ime::ShorthandFileVersion first_rule{
        true, 102, 24};

    assert_true(
        vn_ime::ShouldReloadShorthandFile(std::nullopt, missing),
        "Shorthand reload initializes a missing-file version");
    assert_true(
        !vn_ime::ShouldReloadShorthandFile(first, same),
        "Unchanged shorthand file avoids redundant reload");
    assert_true(
        vn_ime::ShouldReloadShorthandFile(first, changed_time),
        "Shorthand last-write change requests reload");
    assert_true(
        vn_ime::ShouldReloadShorthandFile(first, first_rule),
        "Adding the first shorthand rule requests reload");
    assert_true(
        vn_ime::ShouldReloadShorthandFile(first_rule, missing),
        "Deleting the shorthand file requests a clearing reload");
    assert_true(
        !vn_ime::ShouldReloadShorthandFile(first, std::nullopt),
        "Unavailable file metadata fails closed to the loaded table");

    wchar_t module_path[MAX_PATH] = {};
    const DWORD module_length = GetModuleFileNameW(
        nullptr, module_path, static_cast<DWORD>(std::size(module_path)));
    const auto module_version = module_length > 0
        ? vn_ime::ReadShorthandFileVersion(module_path)
        : std::nullopt;
    assert_true(
        module_version && module_version->exists &&
            module_version->size > 0,
        "Shorthand file version reads real Windows file metadata");
    const auto missing_version = module_length > 0
        ? vn_ime::ReadShorthandFileVersion(
              std::wstring(module_path) + L".missing")
        : std::nullopt;
    assert_true(
        missing_version && !missing_version->exists,
        "Shorthand file version distinguishes a missing file");
    assert_true(
        !vn_ime::ReadShorthandFileVersion(L""),
        "Shorthand file version rejects an empty path");
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

void test_word_direct_inline_casing_sync() {
    std::cout << "\nRunning test_word_direct_inline_casing_sync..." << std::endl;

    Engine vni(InputMethod::VNI);
    type_string(vni, L"su");
    assert_true(vni.UpdateCasingFromHost(L"Su"),
                "Word list title-case rewrite is accepted for VNI");
    type_string(vni, L"73");
    assert_eq(vni.GetDisplayString(), L"S\u1EED",
              "VNI keeps raw state after Word capitalizes list-item text");

    vni.Clear();
    type_string(vni, L"lam");
    assert_true(vni.UpdateCasingFromHost(L"Lam"),
                "Word list title-case rewrite is accepted before a VNI tone key");
    vni.ProcessKey(L'2');
    assert_eq(vni.GetDisplayString(), L"L\u00E0m",
              "VNI lam2 remains convertible after Word capitalization");

    Engine telex(InputMethod::Telex);
    type_string(telex, L"su");
    assert_true(telex.UpdateCasingFromHost(L"Su"),
                "Word list title-case rewrite is accepted for Telex");
    type_string(telex, L"wr");
    assert_eq(telex.GetDisplayString(), L"S\u1EED",
              "Telex keeps raw state after Word capitalizes list-item text");

    telex.Clear();
    type_string(telex, L"lam");
    assert_true(telex.UpdateCasingFromHost(L"Lam"),
                "Word list title-case rewrite is accepted before a Telex tone key");
    telex.ProcessKey(L'f');
    assert_eq(telex.GetDisplayString(), L"L\u00E0m",
              "Telex lamf remains convertible after Word capitalization");

    Engine mismatch(InputMethod::VNI);
    type_string(mismatch, L"su");
    assert_true(!mismatch.UpdateCasingFromHost(L"Xa"),
                "Direct inline casing sync rejects a real host text change");
    assert_eq(mismatch.GetRawString(), L"su",
              "Rejected host text change leaves raw state untouched");

    Engine unsupported_casing(InputMethod::VNI);
    type_string(unsupported_casing, L"su");
    assert_true(!unsupported_casing.UpdateCasingFromHost(L"SU"),
                "Direct inline casing sync rejects non-title-case rewrites");
    assert_eq(unsupported_casing.GetRawString(), L"su",
              "Rejected non-title-case rewrite is transactional");
}

void test_word_direct_inline_edit_session_recovery() {
    std::cout << "\nRunning test_word_direct_inline_edit_session_recovery..."
              << std::endl;

    assert_true(
        vn_ime::DecideWordEditSessionDispatch(
            true, true, false, true) ==
            vn_ime::WordEditSessionDispatch::RetryAsync,
        "Word retries asynchronously when a synchronous edit is unavailable");
    assert_true(
        vn_ime::DecideWordEditSessionDispatch(
            false, true, false, true) ==
                vn_ime::WordEditSessionDispatch::Failed &&
            vn_ime::DecideWordEditSessionDispatch(
                true, false, false, true) ==
                vn_ime::WordEditSessionDispatch::Failed &&
            vn_ime::DecideWordEditSessionDispatch(
                true, true, true, false) ==
                vn_ime::WordEditSessionDispatch::Completed,
        "Async fallback stays scoped to Word TS_E_SYNCHRONOUS failures");
    assert_true(
        vn_ime::IsAcceptedWordAsyncEditSession(true, true) &&
            !vn_ime::IsAcceptedWordAsyncEditSession(false, true) &&
            !vn_ime::IsAcceptedWordAsyncEditSession(true, false),
        "Word consumes a key only after the async request is accepted");

    assert_true(
        vn_ime::ShouldConsumeDirectInlineMutation(true, false) &&
            vn_ime::ShouldConsumeDirectInlineMutation(false, true) &&
            !vn_ime::ShouldConsumeDirectInlineMutation(false, false),
        "A completed text mutation stays consumed even if caret placement fails");

    using vn_ime::WordReconversionContinuation;
    assert_true(
        vn_ime::DecideWordReconversionContinuation(
            true, false, false, true) ==
                WordReconversionContinuation::ProcessChar &&
            vn_ime::DecideWordReconversionContinuation(
                true, false, true, false) ==
                WordReconversionContinuation::Backspace &&
            vn_ime::DecideWordReconversionContinuation(
                false, false, false, true) ==
                WordReconversionContinuation::None &&
            vn_ime::DecideWordReconversionContinuation(
                true, true, false, true) ==
                WordReconversionContinuation::None,
        "Only an active Word typed-reconversion continues text and Backspace keys");

    Engine first_word_vni(InputMethod::VNI);
    type_string(first_word_vni, L"ki");
    assert_true(first_word_vni.UpdateCasingFromHost(L"Ki"),
                "Word first-word title casing is accepted before VNI continuation");
    type_string(first_word_vni, L"e63m");
    assert_eq(first_word_vni.GetDisplayString(), L"Ki\u1EC3m",
              "Word first-word VNI reconversion continues through the tone key");

    Engine first_word_telex(InputMethod::Telex);
    type_string(first_word_telex, L"ki");
    assert_true(first_word_telex.UpdateCasingFromHost(L"Ki"),
                "Word first-word title casing is accepted before Telex continuation");
    type_string(first_word_telex, L"eerm");
    assert_eq(first_word_telex.GetDisplayString(), L"Ki\u1EC3m",
              "Word first-word Telex reconversion continues through the tone key");

    struct WordVniCase {
        std::wstring_view raw;
        std::wstring_view expected;
    };
    for (const WordVniCase& test_case : {
             WordVniCase{L"my4", L"m\u1EF9"},
             WordVniCase{L"linh1", L"l\u00EDnh"},
             WordVniCase{L"kie63m", L"ki\u1EC3m"},
             WordVniCase{L"bo56", L"b\u1ED9"},
             WordVniCase{L"go4", L"g\u00F5"},
         }) {
        Engine engine(InputMethod::VNI);
        type_string(engine, test_case.raw);
        assert_eq(engine.GetDisplayString(), std::wstring(test_case.expected),
                  "VNI Word direct-inline sequence remains convertible");
    }

    struct WordTelexCase {
        std::wstring_view raw;
        std::wstring_view expected;
    };
    for (const WordTelexCase& test_case : {
             WordTelexCase{L"myx", L"m\u1EF9"},
             WordTelexCase{L"linhs", L"l\u00EDnh"},
             WordTelexCase{L"kieemr", L"ki\u1EC3m"},
             WordTelexCase{L"booj", L"b\u1ED9"},
             WordTelexCase{L"gox", L"g\u00F5"},
         }) {
        Engine engine(InputMethod::Telex);
        type_string(engine, test_case.raw);
        assert_eq(engine.GetDisplayString(), std::wstring(test_case.expected),
                  "Telex Word direct-inline sequence remains convertible");
    }
}

void test_composition_length_guard() {
    std::cout << "\nRunning test_composition_length_guard..." << std::endl;

    Engine engine(InputMethod::Telex);
    std::wstring long_raw(kMaxRawKeysPerComposition + 1, L'a');
    type_string(engine, long_raw);
    assert_true(engine.GetRawString().length() == long_raw.length(),
                "Overflow composition keeps the full raw buffer");
    assert_eq(engine.GetDisplayString(), long_raw,
              "Overflow composition displays raw literal text");

    engine.Clear();
    type_string(engine, L"vietes");
    assert_eq(engine.GetDisplayString(), L"vi\u1EBFt",
              "Clear resets overflow bypass state");
}

void test_composition_overflow_backspace_recovery() {
    std::cout << "\nRunning test_composition_overflow_backspace_recovery..." << std::endl;

    Engine engine(InputMethod::Telex);
    std::wstring long_raw(kMaxRawKeysPerComposition + 1, L'b');
    type_string(engine, long_raw);
    assert_eq(engine.GetDisplayString(), long_raw,
              "Overflow composition starts in raw literal bypass");

    assert_true(engine.Backspace(), "Backspace succeeds in overflow composition");
    assert_true(engine.GetRawString().length() == kMaxRawKeysPerComposition,
                "Backspace recovers to the maximum raw length");
    assert_true(engine.BackspaceDisplayChar(), "Display backspace succeeds after recovery");
    assert_true(engine.GetRawString().length() == kMaxRawKeysPerComposition - 1,
                "Display backspace uses raw removal after overflow recovery");

    engine.Clear();
    type_string(engine, L"hoangf");
    assert_eq(engine.GetDisplayString(), L"ho\u00E0ng",
              "Engine parses normally after overflow recovery and clear");
}

void test_reconversion_length_guard() {
    std::cout << "\nRunning test_reconversion_length_guard..." << std::endl;

    std::wstring long_token(kMaxRawKeysPerComposition + 1, L'a');
    assert_true(!BuildReconversionEdit(long_token, long_token.length(), long_token.length(), L's', InputMethod::Telex).has_value(),
                "Long reconversion token is rejected");

    auto hoang = BuildReconversionEdit(L"hoang", 5, 5, L'f', InputMethod::Telex);
    assert_true(hoang.has_value(), "Short reconversion token remains enabled");
    if (hoang) {
        assert_eq(hoang->replacement, L"ho\u00E0ng", "Short reconversion hoang + f");
    }

    std::wstring telex_viet = L"v\u00EDt";
    size_t telex_viet_caret = 2;
    auto insert_e = BuildReconversionEdit(telex_viet, telex_viet_caret, telex_viet_caret, L'e', InputMethod::Telex);
    assert_true(insert_e.has_value(), "Short Telex vit + e remains enabled");
    if (insert_e) {
        telex_viet.replace(insert_e->start, insert_e->end - insert_e->start, insert_e->replacement);
        telex_viet_caret = insert_e->start + insert_e->selection_start;
    }
    auto apply_e = BuildReconversionEdit(telex_viet, telex_viet_caret, telex_viet_caret, L'e', InputMethod::Telex);
    assert_true(apply_e.has_value(), "Short Telex viet + e remains enabled");
    if (apply_e) {
        telex_viet.replace(apply_e->start, apply_e->end - apply_e->start, apply_e->replacement);
    }
    assert_eq(telex_viet, L"vi\u1EBFt", "Short Telex vit + e + e still works");

    auto doan = BuildReconversionEdit(L"\u0111\u00F2n", 2, 2, L'a', InputMethod::Telex);
    assert_true(doan.has_value(), "Short Telex don + a remains enabled");
    if (doan) {
        assert_eq(doan->replacement, L"\u0111o\u00E0n", "Short Telex don + a still works");
    }
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

void test_long_token_guard_latency() {
    std::cout << "\nRunning test_long_token_guard_latency..." << std::endl;

    constexpr int iterations = 1000;
    const std::wstring long_raw(kMaxRawKeysPerComposition + 64, L'a');
    const auto start = std::chrono::steady_clock::now();
    size_t total_keys = 0;
    for (int i = 0; i < iterations; ++i) {
        Engine engine(InputMethod::Telex);
        type_string(engine, long_raw);
        total_keys += long_raw.length();
    }
    const auto end = std::chrono::steady_clock::now();
    const auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    const double average_us = static_cast<double>(elapsed_ns) / 1000.0 / static_cast<double>(total_keys);

    std::cout << "  [INFO] Average long-token guarded key latency: " << average_us << " microseconds" << std::endl;
    assert_true(average_us < 1000.0, "Long-token guarded key latency is under 1.0 ms");
}

void test_long_reconversion_candidate_latency() {
    std::cout << "\nRunning test_long_reconversion_candidate_latency..." << std::endl;

    constexpr int iterations = 100000;
    const std::wstring long_token(kMaxRawKeysPerComposition + 1, L'a');
    size_t rejected = 0;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < iterations; ++i) {
        if (!BuildReconversionEdit(long_token, long_token.length(), long_token.length(), L's', InputMethod::Telex)) {
            ++rejected;
        }
    }
    const auto end = std::chrono::steady_clock::now();
    const auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    const double average_us = static_cast<double>(elapsed_ns) / 1000.0 / iterations;

    std::cout << "  [INFO] Average long reconversion rejection latency: " << average_us << " microseconds" << std::endl;
    assert_true(rejected == iterations && average_us < 1000.0,
                "Long reconversion candidate rejection is under 1.0 ms");
}

void test_esc_restore_capture_predicate() {
    std::cout << "\nRunning test_esc_restore_capture_predicate..." << std::endl;
    // Captures raw != display
    assert_true(vn_ime::ShouldCaptureCommitUndo(L"vies", L"viết"), "Should capture raw != display");
    // Rejects empty raw/display
    assert_true(!vn_ime::ShouldCaptureCommitUndo(L"", L"viết"), "Reject empty raw");
    assert_true(!vn_ime::ShouldCaptureCommitUndo(L"vies", L""), "Reject empty display");
    // Captures raw == display for Backspace undo-commit (e.g. 'xai')
    assert_true(vn_ime::ShouldCaptureCommitUndo(L"github", L"github"), "Capture raw == display");
    // Rejects raw overflow (> 128)
    std::wstring long_raw(129, L'a');
    assert_true(!vn_ime::ShouldCaptureCommitUndo(long_raw, L"viết"), "Reject raw overflow");
    std::wstring long_display(
        vn_ime::kMaxCommitUndoDisplayChars + 1, L'a');
    assert_true(!vn_ime::ShouldCaptureCommitUndo(L"abbr", long_display),
                "Reject oversized shorthand display capture");
}

void test_commit_undo_backspace_restore_gate_and_boundary_spans() {
    std::cout << "\nRunning test_commit_undo_backspace_restore_gate_and_boundary_spans..." << std::endl;

    vn_ime::CommitUndoEntry transformed;
    transformed.raw_keys = L"vies";
    transformed.display_text = L"vi\u1EBFt";
    transformed.committed_tick = 1000;

    assert_true(vn_ime::ShouldRouteCommitUndoBackspace(
                    transformed, 11000, false, true, true, true),
                "Backspace restore gate accepts transformed entry within 10 seconds");
    assert_true(!vn_ime::ShouldRouteCommitUndoBackspace(
                    transformed, 11001, false, true, true, true),
                "Backspace restore gate rejects expired entry");
    assert_true(!vn_ime::ShouldRouteCommitUndoBackspace(
                    transformed, 999, false, true, true, true),
                "Backspace restore gate rejects clock before commit");
    assert_true(!vn_ime::ShouldRouteCommitUndoBackspace(
                    transformed, 11000, true, true, true, true),
                "Backspace restore gate rejects active composition");
    assert_true(!vn_ime::ShouldRouteCommitUndoBackspace(
                    transformed, 11000, false, false, true, true),
                "Backspace restore gate rejects modifier");
    assert_true(!vn_ime::ShouldRouteCommitUndoBackspace(
                    transformed, 11000, false, true, false, true),
                "Backspace restore gate rejects focus mismatch");
    assert_true(!vn_ime::ShouldRouteCommitUndoBackspace(
                    transformed, 11000, false, true, true, false),
                "Backspace restore gate rejects unsupported host");

    vn_ime::CommitUndoEntry unchanged;
    unchanged.raw_keys = L"github";
    unchanged.display_text = L"github";
    unchanged.committed_tick = 1000;
    assert_true(vn_ime::ShouldRouteCommitUndoBackspace(
                    unchanged, 11000, false, true, true, true),
                "Backspace restore gate accepts raw equal to display");

    vn_ime::CommitUndoEntry telegram_entry = transformed;
    telegram_entry.is_tsf = true;
    assert_true(vn_ime::ShouldRouteCommitUndoBackspace(
                    telegram_entry, 11000, false, true, false, true,
                    vn_ime::CommitUndoFocusMode::TelegramTsfContext, true),
                "Telegram TSF restore accepts HWND mismatch with same context");
    assert_true(!vn_ime::ShouldRouteCommitUndoBackspace(
                    telegram_entry, 11000, false, true, false, true,
                    vn_ime::CommitUndoFocusMode::ExactWindow, true),
                "Generic TSF restore rejects HWND mismatch");
    assert_true(!vn_ime::ShouldRouteCommitUndoBackspace(
                    unchanged, 11000, false, true, false, true,
                    vn_ime::CommitUndoFocusMode::ExactWindow, false),
                "Direct restore rejects HWND mismatch");

    telegram_entry.committed_with_ascii_space = true;
    assert_true(
        vn_ime::ShouldRouteTelegramNativeBoundaryBackspace(
            telegram_entry, 11000, false, true, true, true, true, true),
        "Telegram committed-Space entry routes to native boundary resume");
    telegram_entry.committed_with_ascii_space = false;
    assert_true(
        !vn_ime::ShouldRouteTelegramNativeBoundaryBackspace(
            telegram_entry, 11000, false, true, true, true, true, true),
        "Telegram non-Space commit does not route native boundary resume");
    telegram_entry.committed_with_ascii_space = true;
    assert_true(
        !vn_ime::ShouldRouteTelegramNativeBoundaryBackspace(
            telegram_entry, 11000, false, true, false, true, true, true),
        "Non-Telegram host does not route native boundary resume");
    assert_true(
        !vn_ime::ShouldRouteTelegramNativeBoundaryBackspace(
            telegram_entry, 11001, false, true, true, true, true, true),
        "Telegram native boundary route rejects timeout");
    assert_true(
        !vn_ime::ShouldRouteTelegramNativeBoundaryBackspace(
            telegram_entry, 11000, false, true, true, false, true, true),
        "Telegram native boundary route rejects context or focus mismatch");
    assert_true(
        !vn_ime::ShouldRouteTelegramNativeBoundaryBackspace(
            telegram_entry, 11000, false, true, true, true, false, true),
        "Telegram native boundary route rejects unsafe focus context");
    assert_true(
        !vn_ime::ShouldRouteTelegramNativeBoundaryBackspace(
            telegram_entry, 11000, false, true, true, true, true, false),
        "Telegram native boundary route requires stored committed word range");
    assert_true(
        vn_ime::IsTelegramNativeTransactionMarker(
            vn_ime::kTelegramNativeTransactionMarker),
        "Tagged Telegram native transaction marker is recognized");
    assert_true(
        !vn_ime::IsTelegramNativeTransactionMarker(
            static_cast<ULONG_PTR>(0xDEADC0DEu)),
        "Generic synthetic marker is not a Telegram transaction marker");

    assert_true(
        vn_ime::IsTelegramRawReplayMarker(
            vn_ime::kTelegramRawReplayMarker),
        "Telegram raw replay marker is recognized");
    const auto lower_replay = vn_ime::BuildTelegramRawReplayPlan(
        L"te1", false, kMaxRawKeysPerComposition);
    assert_true(
        lower_replay && lower_replay->size() == 3 &&
            (*lower_replay)[0].virtual_key == 'T' &&
            !(*lower_replay)[0].shift_down &&
            (*lower_replay)[1].virtual_key == 'E' &&
            !(*lower_replay)[1].shift_down &&
            (*lower_replay)[2].virtual_key == '1' &&
            !(*lower_replay)[2].shift_down,
        "Telegram replay maps lowercase VNI raw keys without Shift");
    assert_true(
        lower_replay &&
            vn_ime::IsTelegramRawReplayVirtualKey('T', *lower_replay) &&
            vn_ime::IsTelegramRawReplayVirtualKey('1', *lower_replay) &&
            !vn_ime::IsTelegramRawReplayVirtualKey(
                VK_SHIFT, *lower_replay) &&
            !vn_ime::IsTelegramRawReplayVirtualKey('Q', *lower_replay),
        "Telegram replay recognizes only expected marker-lost keys");
    const auto caps_lower_replay = vn_ime::BuildTelegramRawReplayPlan(
        L"hoa", true, kMaxRawKeysPerComposition);
    assert_true(
        caps_lower_replay && caps_lower_replay->size() == 3 &&
            (*caps_lower_replay)[0].shift_down &&
            (*caps_lower_replay)[1].shift_down &&
            (*caps_lower_replay)[2].shift_down,
        "Telegram replay inverts Caps Lock for lowercase raw keys");
    const auto upper_replay = vn_ime::BuildTelegramRawReplayPlan(
        L"Te", false, kMaxRawKeysPerComposition);
    assert_true(
        upper_replay && (*upper_replay)[0].shift_down &&
            !(*upper_replay)[1].shift_down,
        "Telegram replay preserves mixed-case raw keys");
    assert_true(
        upper_replay &&
            vn_ime::IsTelegramRawReplayVirtualKey(
                VK_LSHIFT, *upper_replay) &&
            vn_ime::IsTelegramRawReplayVirtualKey(
                VK_RSHIFT, *upper_replay),
        "Telegram replay recognizes marker-lost Shift variants");
    const auto caps_upper_replay = vn_ime::BuildTelegramRawReplayPlan(
        L"T", true, kMaxRawKeysPerComposition);
    assert_true(
        caps_upper_replay && !(*caps_upper_replay)[0].shift_down,
        "Telegram replay uses Caps Lock directly for uppercase raw keys");
    assert_true(
        !vn_ime::BuildTelegramRawReplayPlan(
            L"te-", false, kMaxRawKeysPerComposition),
        "Telegram replay rejects unsupported punctuation");
    assert_true(
        !vn_ime::BuildTelegramRawReplayPlan(
            L"tê", false, kMaxRawKeysPerComposition),
        "Telegram replay rejects non-ASCII display text");
    assert_true(
        !vn_ime::BuildTelegramRawReplayPlan(
            std::wstring(kMaxRawKeysPerComposition + 1, L'a'), false,
            kMaxRawKeysPerComposition),
        "Telegram replay rejects overlong raw input");

    assert_true(
        vn_ime::ShouldInvalidateCommitUndoOnTestKeyDown(
            VK_SPACE, false, false, false),
        "A second Space invalidates Telegram commit undo");
    assert_true(
        vn_ime::ShouldInvalidateCommitUndoOnTestKeyDown(
            VK_LEFT, false, false, false),
        "Navigation invalidates Telegram commit undo");
    assert_true(
        vn_ime::ShouldInvalidateCommitUndoOnTestKeyDown(
            'A', false, false, false),
        "Intervening text invalidates Telegram commit undo");
    assert_true(
        !vn_ime::ShouldInvalidateCommitUndoOnTestKeyDown(
            VK_BACK, false, false, false),
        "Immediate Backspace preserves Telegram commit undo");
    assert_true(
        !vn_ime::ShouldInvalidateCommitUndoOnTestKeyDown(
            VK_ESCAPE, false, false, false),
        "Esc preserves raw restore eligibility");
    assert_true(
        !vn_ime::ShouldInvalidateCommitUndoOnTestKeyDown(
            VK_SHIFT, true, false, false),
        "Modifier-only input preserves Telegram commit undo");
    assert_true(
        !vn_ime::ShouldInvalidateCommitUndoOnTestKeyDown(
            VK_SPACE, false, true, false),
        "Pending Telegram boundary transaction owns its synthetic keys");
    assert_true(
        !vn_ime::ShouldInvalidateCommitUndoOnTestKeyDown(
            'T', false, false, true),
        "Trusted Telegram raw replay does not invalidate its own state");

    const vn_ime::TelegramRawReplayKey plain_key{
        .virtual_key = 'T', .shift_down = false};
    assert_true(
        !vn_ime::DecideTelegramRawReplaySend(plain_key, 0).complete &&
            vn_ime::DecideTelegramRawReplaySend(plain_key, 1)
                .cleanup_key_up &&
            vn_ime::DecideTelegramRawReplaySend(plain_key, 2).complete,
        "Telegram plain replay key has bounded partial-send cleanup");
    const vn_ime::TelegramRawReplayKey shifted_key{
        .virtual_key = 'T', .shift_down = true};
    const auto shifted_one =
        vn_ime::DecideTelegramRawReplaySend(shifted_key, 1);
    const auto shifted_two =
        vn_ime::DecideTelegramRawReplaySend(shifted_key, 2);
    const auto shifted_three =
        vn_ime::DecideTelegramRawReplaySend(shifted_key, 3);
    assert_true(
        shifted_one.cleanup_shift_up && !shifted_one.cleanup_key_up &&
            shifted_two.cleanup_shift_up && shifted_two.cleanup_key_up &&
            shifted_three.cleanup_shift_up &&
            !shifted_three.cleanup_key_up &&
            vn_ime::DecideTelegramRawReplaySend(shifted_key, 4).complete,
        "Telegram shifted replay key releases every partial modifier state");

    vn_ime::TelegramRawReplayState raw_replay_state;
    assert_true(
        raw_replay_state.Begin(2, 5000, kMaxRawKeysPerComposition) &&
            raw_replay_state.MarkDispatching(5001) &&
            raw_replay_state.Complete() &&
            !raw_replay_state.IsPending(),
        "Telegram raw replay completes one bounded timer lifecycle");
    assert_true(
        raw_replay_state.Begin(2, 6000, kMaxRawKeysPerComposition) &&
            !raw_replay_state.MarkDispatching(
                6000 + vn_ime::kTelegramRawReplayWindowMs + 1) &&
            raw_replay_state.Cancel(),
        "Telegram raw replay rejects an expired timer");
    assert_true(
        !raw_replay_state.Begin(
            kMaxRawKeysPerComposition + 1, 7000,
            kMaxRawKeysPerComposition),
        "Telegram raw replay state rejects overlong plans");

    vn_ime::TelegramSyntheticSelectionSuppressionState suppression_state;
    suppression_state.Begin(2000);
    for (WPARAM virtual_key :
         {static_cast<WPARAM>(VK_BACK),
          static_cast<WPARAM>(VK_CONTROL),
          static_cast<WPARAM>(VK_LCONTROL),
          static_cast<WPARAM>(VK_RCONTROL),
          static_cast<WPARAM>(VK_SHIFT),
          static_cast<WPARAM>(VK_LSHIFT),
          static_cast<WPARAM>(VK_RSHIFT),
          static_cast<WPARAM>(VK_LEFT)}) {
        assert_true(
            suppression_state.ShouldPassThrough(
                vn_ime::TelegramBoundaryResumePhase::TimerScheduled,
                2099, virtual_key),
            "Expected Telegram selection key survives a lost marker");
    }
    assert_true(
        !suppression_state.ShouldPassThrough(
            vn_ime::TelegramBoundaryResumePhase::TimerScheduled,
            2099, static_cast<WPARAM>('1')),
        "Arbitrary real key is never hidden by Telegram suppression");
    assert_true(
        suppression_state.ShouldPassThrough(
            vn_ime::TelegramBoundaryResumePhase::ResumeRequested,
            2099, VK_LEFT),
        "Late Telegram selection key survives after resume is requested");
    assert_true(
        !suppression_state.ShouldPassThrough(
            vn_ime::TelegramBoundaryResumePhase::SelectionVerified,
            2099, VK_LEFT),
        "Telegram suppression stops when selection verification begins");
    assert_true(
        !suppression_state.ShouldPassThrough(
            vn_ime::TelegramBoundaryResumePhase::TimerScheduled,
            2101, VK_LEFT),
        "Telegram lost-marker suppression expires at its deadline");
    suppression_state.Clear();
    assert_true(
        !suppression_state.ShouldPassThrough(
            vn_ime::TelegramBoundaryResumePhase::TimerScheduled,
            2000, VK_BACK),
        "Cleared Telegram suppression is idempotently inactive");

    for (UINT sent_count = 0; sent_count <= 8; ++sent_count) {
        const auto decision =
            vn_ime::DecideTelegramNativeSelectionSend(sent_count);
        assert_true(
            decision.consume_physical_backspace == (sent_count >= 1) &&
                decision.selection_complete == (sent_count == 8) &&
                decision.cleanup_required ==
                    (sent_count >= 1 && sent_count < 8) &&
                decision.partial_selection_may_be_active ==
                    (sent_count >= 5 && sent_count < 8),
            "Telegram partial native send count has a safe disposition");
    }

    vn_ime::TelegramBoundaryResumeState boundary_state;
    assert_true(!boundary_state.IsPending(), "Telegram boundary resume starts idle");
    assert_true(boundary_state.Begin(1200),
                "Telegram boundary timer schedules from idle");
    assert_true(
        boundary_state.IsPending() &&
            boundary_state.phase ==
                vn_ime::TelegramBoundaryResumePhase::TimerScheduled,
        "Physical Backspace begins scheduled Telegram boundary state");
    assert_true(!boundary_state.Begin(1201),
                "Telegram boundary timer cannot schedule twice");
    assert_true(boundary_state.MarkResumeRequested(),
                "Timer callback advances pending resume request");
    assert_true(!boundary_state.MarkResumeRequested(),
                "Telegram resume request is issued only once");
    assert_true(boundary_state.MarkSelectionVerified(),
                "Exact live selection advances Telegram transaction");
    assert_true(boundary_state.MarkTextDeleted(),
                "Verified Telegram selection can enter replacement phase");
    assert_true(boundary_state.MarkCompositionStarted(),
                "Telegram replacement starts composition once");
    assert_true(boundary_state.Complete(),
                "Successful Telegram transaction returns to idle");
    assert_true(!boundary_state.IsPending(),
                "Completed Telegram transaction is no longer pending");
    assert_true(boundary_state.Begin(1300),
                "Telegram boundary state can begin after completion");
    assert_true(boundary_state.Cancel(),
                "Pending Telegram resume can be canceled");
    assert_true(!boundary_state.IsPending() && boundary_state.started_tick == 0,
                "Pending Telegram boundary state resets to idle");
    assert_true(!boundary_state.Cancel(),
                "Canceling idle Telegram resume is idempotent");

    vn_ime::TelegramBoundaryResumeState retry_state;
    assert_true(retry_state.Begin(3000),
                "Telegram selection retry starts from idle");
    for (unsigned attempt = 1;
         attempt <= vn_ime::kTelegramSelectionMaxProbeAttempts;
         ++attempt) {
        assert_true(retry_state.MarkResumeRequested(),
                    "Telegram selection probe request stays within its cap");
        assert_true(retry_state.selection_probe_attempts == attempt,
                    "Telegram selection probe count advances exactly once");
        if (attempt < vn_ime::kTelegramSelectionMaxProbeAttempts) {
            assert_true(
                retry_state.MarkSelectionRetryScheduled(
                    3000 + attempt * 10),
                "Empty Telegram selection reschedules within its deadline");
            assert_true(
                !retry_state.MarkSelectionRetryScheduled(
                    3000 + attempt * 10),
                "Telegram selection retry scheduling is idempotent");
        }
    }
    assert_true(
        !retry_state.MarkSelectionRetryScheduled(3060),
        "Telegram selection retry stops at the probe cap");
    assert_true(retry_state.Cancel() &&
                    retry_state.selection_probe_attempts == 0,
                "Cancel resets Telegram selection retry accounting");

    vn_ime::TelegramBoundaryResumeState expired_retry_state;
    assert_true(
        expired_retry_state.Begin(4000) &&
            expired_retry_state.MarkResumeRequested() &&
            !expired_retry_state.MarkSelectionRetryScheduled(
                4000 + vn_ime::kTelegramSelectionRetryWindowMs + 1),
        "Telegram selection retry rejects probes after its deadline");

    assert_true(
        vn_ime::IsVerifiedTelegramNativeSelection(
            L"te", L"te", true, kMaxRawKeysPerComposition),
        "Telegram native selection verifies te exactly");
    assert_true(
        vn_ime::IsVerifiedTelegramNativeSelection(
            L"hoa", L"hoa", true, kMaxRawKeysPerComposition),
        "Telegram native selection verifies generic hoa exactly");
    assert_true(
        !vn_ime::IsVerifiedTelegramNativeSelection(
            L",hoa", L"hoa", true, kMaxRawKeysPerComposition),
        "Telegram native selection rejects punctuation outside the token");
    assert_true(
        !vn_ime::IsVerifiedTelegramNativeSelection(
            L"hoa", L"hoas", true, kMaxRawKeysPerComposition),
        "Telegram native selection rejects changed display text");
    assert_true(
        !vn_ime::IsVerifiedTelegramNativeSelection(
            L"te", L"te", false, kMaxRawKeysPerComposition),
        "Telegram native selection requires a non-empty host selection");

    assert_true(
        vn_ime::DecideTelegramVerifiedTransactionRecovery(
            true, true, true, true, true, true) ==
            vn_ime::TelegramVerifiedTransactionRecovery::KeepComposition,
        "Complete Telegram verified transaction keeps one composition");
    assert_true(
        vn_ime::DecideTelegramVerifiedTransactionRecovery(
            false, false, false, false, false, false) ==
            vn_ime::TelegramVerifiedTransactionRecovery::CollapseSelectionToEnd,
        "Mismatched Telegram selection is collapsed without deletion");
    assert_true(
        vn_ime::DecideTelegramVerifiedTransactionRecovery(
            true, false, false, false, false, false) ==
            vn_ime::TelegramVerifiedTransactionRecovery::CollapseSelectionToEnd,
        "Telegram pre-delete failure collapses the verified native selection");
    assert_true(
        vn_ime::DecideTelegramVerifiedTransactionRecovery(
            true, true, false, false, false, false) ==
            vn_ime::TelegramVerifiedTransactionRecovery::ReplaceTransactionRangeWithDisplay,
        "Telegram start failure restores the deleted display by replacement");
    assert_true(
        vn_ime::DecideTelegramVerifiedTransactionRecovery(
            true, true, true, true, true, false) ==
        vn_ime::TelegramVerifiedTransactionRecovery::ReplaceTransactionRangeWithDisplay,
        "Telegram caret failure replaces the tracked range instead of duplicating text");

    vn_ime::TelegramBoundaryResumeState invalid_transition_state;
    assert_true(
        invalid_transition_state.Begin(1400) &&
            invalid_transition_state.MarkResumeRequested() &&
            !invalid_transition_state.MarkTextDeleted(),
        "Telegram state rejects deletion transition before selection verification");
    assert_true(
        vn_ime::DecideTelegramVerifiedTransactionRecovery(
            true, true, false, false, false, false) ==
            vn_ime::TelegramVerifiedTransactionRecovery::ReplaceTransactionRangeWithDisplay,
        "Actual Telegram deletion rolls back even when state transition fails");

    {
        const auto span = vn_ime::FindVerifiedTokenAtLookbehindEnd(
            L"te", L"te", false, kMaxRawKeysPerComposition);
        assert_true(span && span->start == 0 && span->end == 2,
                    "Telegram lookbehind selects te at context start");
    }
    {
        const auto span = vn_ime::FindVerifiedTokenAtLookbehindEnd(
            L"prefix hoa", L"hoa", false,
            kMaxRawKeysPerComposition);
        assert_true(span && span->start == 7 && span->end == 10,
                    "Telegram lookbehind selects generic hoa token");
    }
    {
        const auto span = vn_ime::FindVerifiedTokenAtLookbehindEnd(
            L"abc,hoa", L"hoa", false,
            kMaxRawKeysPerComposition);
        assert_true(span && span->start == 4 && span->end == 7,
                    "Telegram lookbehind stops at punctuation");
    }
    assert_true(
        !vn_ime::FindVerifiedTokenAtLookbehindEnd(
            L"abc hoa ", L"hoa", false,
            kMaxRawKeysPerComposition),
        "Telegram lookbehind rejects caret after whitespace");
    {
        const std::wstring max_token(
            kMaxRawKeysPerComposition, L'a');
        assert_true(
            vn_ime::FindVerifiedTokenAtLookbehindEnd(
                max_token, max_token, false,
                kMaxRawKeysPerComposition).has_value(),
            "Telegram lookbehind accepts max-length token at context start");
        assert_true(
            !vn_ime::FindVerifiedTokenAtLookbehindEnd(
                max_token, max_token, true,
                kMaxRawKeysPerComposition),
            "Telegram lookbehind rejects left-truncated max-length token");

        const std::wstring bounded_token = L"," + max_token;
        const auto bounded_span = vn_ime::FindVerifiedTokenAtLookbehindEnd(
            bounded_token, max_token, true,
            kMaxRawKeysPerComposition);
        assert_true(
            bounded_span && bounded_span->start == 1 &&
                bounded_span->end == bounded_token.length(),
            "Telegram lookbehind accepts boundary sentinel plus max-length token");

        const std::wstring spaced_token = L" " + max_token;
        const auto spaced_span = vn_ime::FindVerifiedTokenAtLookbehindEnd(
            spaced_token, max_token, true,
            kMaxRawKeysPerComposition);
        assert_true(
            spaced_span && spaced_span->start == 1 &&
                spaced_span->end == spaced_token.length(),
            "Telegram lookbehind accepts whitespace sentinel plus max-length token");

        const std::wstring overlong_token(
            kMaxRawKeysPerComposition + 1, L'a');
        assert_true(
            !vn_ime::FindVerifiedTokenAtLookbehindEnd(
                overlong_token, max_token, false,
                kMaxRawKeysPerComposition),
            "Telegram lookbehind rejects overlong token without a boundary");
    }
    assert_true(
        !vn_ime::FindVerifiedTokenAtLookbehindEnd(
            L"hoa", L"hoà", false,
            kMaxRawKeysPerComposition),
        "Telegram lookbehind rejects mismatched committed display");

    assert_true(
        vn_ime::DecideTelegramBoundaryResumeDisposition(
            true, true, true, true, true) ==
            vn_ime::TelegramBoundaryResumeDisposition::ResumeComposition,
        "Verified Telegram native boundary resumes composition");
    assert_true(
        vn_ime::DecideTelegramBoundaryResumeDisposition(
            true, true, false, true, true) ==
            vn_ime::TelegramBoundaryResumeDisposition::PreserveNativeResult,
        "Failed Telegram resume preserves native Backspace result");

    assert_true(
        vn_ime::CanUseStoredTsfRangeFallback(true, true, true, true, true),
        "Telegram stored-range fallback accepts fully verified word, boundary, and caret");
    assert_true(
        !vn_ime::CanUseStoredTsfRangeFallback(false, true, true, true, true),
        "Stored-range fallback remains Telegram-only");
    assert_true(
        !vn_ime::CanUseStoredTsfRangeFallback(true, false, true, true, true),
        "Stored-range fallback is not used when selection verification remains readable");
    assert_true(
        !vn_ime::CanUseStoredTsfRangeFallback(true, true, false, true, true),
        "Stored-range fallback rejects changed committed word text");
    assert_true(
        !vn_ime::CanUseStoredTsfRangeFallback(true, true, true, false, true),
        "Stored-range fallback rejects a non-Space boundary");
    assert_true(
        !vn_ime::CanUseStoredTsfRangeFallback(true, true, true, true, false),
        "Stored-range fallback rejects a caret away from the boundary end");

    assert_true(
        vn_ime::DecideCommitUndoResumeDisposition(true, true, true, true) ==
            vn_ime::CommitUndoResumeDisposition::ResumeComposition,
        "Telegram restore resumes only after composition/update/active/caret success");
    assert_true(
        vn_ime::DecideCommitUndoResumeDisposition(true, false, true, true) ==
            vn_ime::CommitUndoResumeDisposition::Rollback,
        "Telegram restore rolls back after update failure");
    assert_true(
        vn_ime::DecideCommitUndoResumeDisposition(false, true, true, true) ==
            vn_ime::CommitUndoResumeDisposition::Rollback,
        "Telegram restore rolls back when composition start failed");
    assert_true(
        vn_ime::DecideCommitUndoResumeDisposition(true, true, false, true) ==
            vn_ime::CommitUndoResumeDisposition::Rollback,
        "Telegram restore rolls back when active composition is absent");
    assert_true(
        vn_ime::DecideCommitUndoResumeDisposition(true, true, true, false) ==
            vn_ime::CommitUndoResumeDisposition::Rollback,
        "Telegram restore rolls back when direct SetSelection fails");
    assert_true(
        vn_ime::ShouldCaptureCommitUndo(L"te", L"te"),
        "Telegram raw-equal VNI word is eligible for restore capture");
    {
        Engine engine(InputMethod::VNI);
        type_string(engine, L"te");
        assert_eq(engine.GetDisplayString(), L"te",
                  "VNI Telegram resume starts from raw-equal te");
        engine.ProcessKey(L'1');
        assert_eq(engine.GetDisplayString(), L"té",
                  "VNI Telegram resumed te + 1 -> té");
    }
    assert_true(
        vn_ime::IsCommitUndoDocumentCleanupSuccessful(true, true, true),
        "Document cleanup succeeds after text clear, composition end, and active reset");
    assert_true(
        !vn_ime::IsCommitUndoDocumentCleanupSuccessful(true, true, false),
        "Document cleanup fails when active composition remains");
    assert_true(
        !vn_ime::IsCommitUndoDocumentCleanupSuccessful(true, false, true),
        "Document cleanup fails when composition end fails");
    assert_true(
        vn_ime::DecideCommitUndoRollbackDisposition(true, true, true, true, true) ==
            vn_ime::CommitUndoRollbackDisposition::PassThrough,
        "Verified Telegram post-Space rollback passes Backspace through to host");
    assert_true(
        vn_ime::DecideCommitUndoRollbackDisposition(true, true, true, false, true) ==
            vn_ime::CommitUndoRollbackDisposition::ConsumeBackspace,
        "Verified Telegram rollback without a boundary consumes Backspace");
    assert_true(
        vn_ime::DecideCommitUndoRollbackDisposition(true, true, true, false, false) ==
            vn_ime::CommitUndoRollbackDisposition::PassThrough,
        "Verified Telegram Esc rollback passes through without a boundary");
    assert_true(
        vn_ime::DecideCommitUndoRollbackDisposition(false, true, true, true, true) ==
            vn_ime::CommitUndoRollbackDisposition::ConsumeBackspace,
        "Unverified rollback text consumes Backspace fail-closed");
    assert_true(
        vn_ime::DecideCommitUndoRollbackDisposition(true, false, true, true, true) ==
            vn_ime::CommitUndoRollbackDisposition::ConsumeBackspace,
        "Unverified rollback selection consumes Backspace fail-closed");
    assert_true(
        vn_ime::DecideCommitUndoRollbackDisposition(true, true, false, true, true) ==
            vn_ime::CommitUndoRollbackDisposition::ConsumeBackspace,
        "Incomplete composition cleanup consumes Backspace fail-closed");
    assert_true(
        vn_ime::CanConsumeCommitUndoBackspace(true, false, true, false, false),
        "Resumed Telegram composition may consume Backspace");
    assert_true(
        vn_ime::CanConsumeCommitUndoBackspace(false, true, true, true, false),
        "Verified Telegram boundary removal may consume Backspace");
    assert_true(
        vn_ime::CanConsumeCommitUndoBackspace(false, true, true, false, true),
        "Verified Telegram native replay may consume Backspace");
    assert_true(
        !vn_ime::CanConsumeCommitUndoBackspace(false, true, true, false, false),
        "Telegram trailing-space failure cannot consume without handling boundary");
    assert_true(
        vn_ime::CanConsumeCommitUndoBackspace(false, true, false, false, false),
        "Verified no-boundary rollback still protects final character");

    {
        const std::wstring text = L"abc vi\u1EBFt ";
        auto span = vn_ime::FindVerifiedTextBeforeCaretWithOptionalTrailingSpace(
            text, text.length(), L"vi\u1EBFt");
        assert_true(span.has_value() && span->has_trailing_space,
                    "Wide span recognizes display plus trailing Space");
        assert_true(span && span->start == 4 && span->end == text.length(),
                    "Wide trailing Space span uses UTF-16 offsets");
    }
    {
        const std::wstring text = L"abc github ";
        auto span = vn_ime::FindVerifiedTextBeforeCaretWithOptionalTrailingSpace(
            text, text.length(), L"github");
        assert_true(span.has_value() && span->has_trailing_space,
                    "Wide span recognizes unchanged English word plus trailing Space");
        assert_true(span && span->start == 4 && span->end == text.length(),
                    "Wide English trailing Space span bounds");
    }
    {
        const std::wstring text = L"abc vi\u1EC7n ";
        auto span = vn_ime::FindVerifiedTextBeforeCaretWithOptionalTrailingSpace(
            text, text.length(), L"vi\u1EBFt");
        assert_true(!span.has_value(), "Wide span rejects changed text before caret");
    }
    {
        const std::wstring text = L"abc vi\u1EBFt ";
        auto span = vn_ime::FindVerifiedTextBeforeCaretWithOptionalTrailingSpace(
            text, 7, L"vi\u1EBFt");
        assert_true(!span.has_value(), "Wide span rejects a caret at the wrong offset");
    }
    {
        const std::string text = to_utf8(L"abc vi\u1EBFt ");
        const std::string display = to_utf8(L"vi\u1EBFt");
        auto span = vn_ime::FindVerifiedBytesBeforeCaretWithOptionalTrailingSpace(
            text, text.length(), display);
        const size_t expected_start = text.length() - display.length() - 1;
        assert_true(span.has_value() && span->has_trailing_space,
                    "UTF-8 span recognizes Vietnamese display plus trailing Space");
        assert_true(span && span->start == expected_start && span->end == text.length(),
                    "UTF-8 trailing Space span uses byte offsets");
    }
    {
        const std::string text = "abc github ";
        auto span = vn_ime::FindVerifiedBytesBeforeCaretWithOptionalTrailingSpace(
            text, text.length(), "github");
        assert_true(span.has_value() && span->has_trailing_space,
                    "UTF-8 span recognizes raw-equal-display English word plus trailing Space");
        assert_true(span && span->start == 4 && span->end == text.length(),
                    "UTF-8 raw-equal-display trailing Space span bounds");
    }
    {
        const std::wstring text = L"abc vi\u1EBFt";
        auto span = vn_ime::FindVerifiedTextBeforeCaretWithOptionalTrailingSpace(
            text, text.length(), L"vi\u1EBFt");
        assert_true(span.has_value() && !span->has_trailing_space,
                    "Wide optional span accepts exact display without trailing Space");
    }
    {
        const std::string text = "abc github";
        auto span = vn_ime::FindVerifiedBytesBeforeCaretWithOptionalTrailingSpace(
            text, text.length(), "github");
        assert_true(span.has_value() && !span->has_trailing_space,
                    "UTF-8 optional span accepts raw-equal-display without trailing Space");
    }
    {
        const std::string text = to_utf8(L"abc vi\u1EC7n ");
        const std::string display = to_utf8(L"vi\u1EBFt");
        auto span = vn_ime::FindVerifiedBytesBeforeCaretWithOptionalTrailingSpace(
            text, text.length(), display);
        assert_true(!span.has_value(), "UTF-8 span rejects changed text before caret");
    }
}

void test_secure_clear_commit_undo_entry() {
    std::cout << "\nRunning test_secure_clear_commit_undo_entry..." << std::endl;

    vn_ime::CommitUndoEntry entry;
    entry.raw_keys = L"vies";
    entry.display_text = L"viết";
    entry.method = InputMethod::VNI;
    entry.transform_kind =
        vn_ime::CommitUndoEntry::TransformKind::SpellerCorrection;
    entry.selection_generation = 42;
    entry.committed_tick = 1234;
    entry.hwnd = reinterpret_cast<HWND>(0x1234);
    entry.expected_caret_offset = 7;
    entry.is_tsf = true;
    entry.committed_with_ascii_space = true;

    vn_ime::SecureClearCommitUndoEntry(entry);

    assert_true(entry.raw_keys.empty(), "Commit undo raw keys cleared");
    assert_true(entry.display_text.empty(), "Commit undo display text cleared");
    assert_true(entry.method == InputMethod::Telex, "Commit undo method reset");
    assert_true(entry.transform_kind ==
                    vn_ime::CommitUndoEntry::TransformKind::None,
                "Commit undo transform kind reset");
    assert_true(entry.selection_generation == 0, "Commit undo selection generation reset");
    assert_true(entry.committed_tick == 0, "Commit undo tick reset");
    assert_true(entry.hwnd == nullptr, "Commit undo hwnd reset");
    assert_true(entry.expected_caret_offset == 0, "Commit undo caret offset reset");
    assert_true(!entry.is_tsf, "Commit undo TSF flag reset");
    assert_true(!entry.committed_with_ascii_space,
                "Commit undo committed-Space metadata reset");
}

void test_commit_transform_caret_policy() {
    std::cout << "\nRunning test_commit_transform_caret_policy..." << std::endl;

    assert_true(
        vn_ime::ShouldMoveCommitCaretToCompositionEnd(
            vn_ime::CommitCaretPolicy::MoveToCompositionEnd),
        "Keyboard commit keeps explicit move-to-end caret policy");
    assert_true(
        !vn_ime::ShouldMoveCommitCaretToCompositionEnd(
            vn_ime::CommitCaretPolicy::PreserveHostSelection),
        "Mouse or selection commit keeps explicit preserve policy");
    assert_true(
        !vn_ime::NeedsAutoCapitalizeRewrite(L'C', L'C'),
        "Already-uppercase first character does not rewrite composition text");
    assert_true(
        vn_ime::NeedsAutoCapitalizeRewrite(L'c', L'C'),
        "Lowercase first character still requests auto-cap rewrite");
}

void test_dialog_vertical_fit_policy() {
    std::cout << "\nRunning test_dialog_vertical_fit_policy..." << std::endl;

    assert_true(
        vn_ime::ShouldKeepDialogTemplateChildVisible(false, true),
        "Visible template child remains visible before parent is shown");
    assert_true(
        !vn_ime::ShouldKeepDialogTemplateChildVisible(false, false),
        "Explicitly hidden template child remains hidden");
    assert_true(
        vn_ime::ShouldKeepDialogTemplateChildVisible(true, false),
        "Footer remains visible regardless of parent visibility state");

    const auto full = vn_ime::ComputeDialogVerticalFit(900, 840, 900);
    assert_true(!full.needs_scroll && full.footer_top == 840 &&
                    full.max_scroll == 0,
                "Full-height dialog keeps footer and needs no scroll");

    const auto dpi_125 = vn_ime::ComputeDialogVerticalFit(1125, 1050, 900);
    assert_true(dpi_125.needs_scroll && dpi_125.footer_top == 825 &&
                    dpi_125.max_scroll == 225,
                "125 percent constrained work area pins footer and scrolls content");

    const auto dpi_150 = vn_ime::ComputeDialogVerticalFit(1350, 1260, 900);
    assert_true(dpi_150.needs_scroll && dpi_150.footer_top == 810 &&
                    dpi_150.max_scroll == 450,
                "150 percent constrained work area pins footer and scrolls content");
}

void test_smart_undo_metadata_gate_and_transaction() {
    std::cout << "\nRunning test_smart_undo_metadata_gate_and_transaction..." << std::endl;

    auto make_entry = [](vn_ime::CommitUndoEntry::TransformKind kind) {
        vn_ime::CommitUndoEntry entry;
        entry.raw_keys = L"vies";
        entry.display_text = L"vi\u1EBFt";
        entry.transform_kind = kind;
        entry.committed_tick = 1000;
        entry.committed_with_ascii_space = true;
        return entry;
    };

    auto corrected = make_entry(
        vn_ime::CommitUndoEntry::TransformKind::SpellerCorrection);
    const auto routes = [&](const vn_ime::CommitUndoEntry& entry,
                            bool enabled = true,
                            ULONGLONG now = 11000,
                            bool active = false,
                            bool no_modifier = true,
                            bool focus = true,
                            bool context = true,
                            bool selection = true,
                            bool secure = false,
                            bool host = true) {
        return vn_ime::ShouldRouteSmartUndoBackspace(
            entry, enabled, now, active, no_modifier, focus, context,
            selection, secure, host);
    };

    assert_true(routes(corrected),
                "Actual correction plus Space routes Smart Undo");
    assert_true(!routes(corrected, false),
                "Disabled option gates only Smart Undo route");
    assert_true(vn_ime::ShouldRouteCommitUndoBackspace(
                    corrected, 11000, false, true, true, true),
                "Disabled Smart Undo policy does not alter existing resume gate");
    assert_true(!routes(corrected, true, 11001),
                "Smart Undo rejects timeout");
    assert_true(!routes(corrected, true, 11000, true),
                "Smart Undo rejects active composition");
    assert_true(!routes(corrected, true, 11000, false, false),
                "Smart Undo rejects a modifier chord");
    assert_true(!routes(corrected, true, 11000, false, true, false),
                "Smart Undo rejects focus mismatch");
    assert_true(!routes(corrected, true, 11000, false, true, true, false),
                "Smart Undo rejects context mismatch");
    assert_true(!routes(corrected, true, 11000, false, true, true, true, false),
                "Smart Undo rejects non-empty or moved selection");
    assert_true(!routes(corrected, true, 11000, false, true, true, true, true, true),
                "Smart Undo rejects secure input");
    assert_true(!routes(corrected, true, 11000, false, true, true, true, false, false),
                "Smart Undo rejects unsupported host");
    assert_true(vn_ime::ShouldInvalidateCommitUndoOnTestKeyDown(
                    L'A', false, false, false),
                "Intervening real key invalidates commit undo");

    auto unchanged = make_entry(
        vn_ime::CommitUndoEntry::TransformKind::None);
    assert_true(!routes(unchanged),
                "Normal conversion and reconversion metadata do not route Smart Undo");
    unchanged.transform_kind =
        vn_ime::CommitUndoEntry::TransformKind::SpellerCorrection;
    unchanged.raw_keys = unchanged.display_text;
    assert_true(!routes(unchanged),
                "Raw-equal-display entry does not route Smart Undo");
    corrected.committed_with_ascii_space = false;
    assert_true(!routes(corrected),
                "Non-Space commit fails closed for Smart Undo");
    corrected.committed_with_ascii_space = true;

    auto shorthand = make_entry(
        vn_ime::CommitUndoEntry::TransformKind::ShorthandExpansion);
    shorthand.raw_keys = L"vn";
    shorthand.display_text = L"Vi\u1EC7t Nam";
    assert_true(routes(shorthand),
                "Shorthand expansion plus Space routes Smart Undo");
    std::wstring shorthand_text = L"abc Vi\u1EC7t Nam ";
    const auto shorthand_span =
        vn_ime::FindVerifiedSmartUndoTextBeforeCaret(
            shorthand_text, shorthand_text.length(), shorthand);
    if (shorthand_span) {
        shorthand_text.replace(
            shorthand_span->start,
            shorthand_span->end - shorthand_span->start,
            shorthand.raw_keys);
    }
    assert_eq(shorthand_text, L"abc vn",
              "Smart Undo restores shorthand shortcut and removes Space");

    auto segmented = make_entry(
        vn_ime::CommitUndoEntry::TransformKind::WordSegmentation);
    segmented.raw_keys = L"tuttat1";
    segmented.display_text = L"t\u00FAt t\u00E1t";
    assert_true(routes(segmented),
                "Word segmentation plus Space routes Smart Undo");
    std::wstring segmented_text = L"abc t\u00FAt t\u00E1t ";
    const auto segmented_span =
        vn_ime::FindVerifiedSmartUndoTextBeforeCaret(
            segmented_text, segmented_text.length(), segmented);
    assert_true(
        segmented_span.has_value() &&
            segmented_span->has_trailing_space,
        "Smart Undo verifies multiword UTF-16 display plus Space");
    if (segmented_span) {
        segmented_text.replace(
            segmented_span->start,
            segmented_span->end - segmented_span->start,
            segmented.raw_keys);
    }
    assert_eq(segmented_text, L"abc tuttat1",
              "Smart Undo restores segmented VNI raw and removes Space");

    segmented.raw_keys = L"tuttats";
    const std::string segmented_display_utf8 =
        to_utf8(segmented.display_text);
    const std::string segmented_bytes =
        to_utf8(L"abc t\u00FAt t\u00E1t ");
    const auto segmented_byte_span =
        vn_ime::FindVerifiedSmartUndoBytesBeforeCaret(
            segmented_bytes, segmented_bytes.length(),
            segmented_display_utf8, segmented);
    assert_true(
        segmented_byte_span.has_value() &&
            segmented_byte_span->has_trailing_space &&
            segmented_byte_span->end == segmented_bytes.length(),
        "Scintilla UTF-8 Smart Undo span covers segmented text and Space");

    std::wstring text = L"abc vi\u1EBFt ";
    const auto span = vn_ime::FindVerifiedSmartUndoTextBeforeCaret(
        text, text.length(), corrected);
    assert_true(span.has_value() && span->has_trailing_space,
                "Smart Undo verifies corrected Unicode text and trailing Space");
    if (span) {
        text.replace(span->start, span->end - span->start,
                     corrected.raw_keys);
    }
    assert_eq(text, L"abc vies",
              "Smart Undo transaction restores literal raw and removes Space");

    const std::string display_utf8 = to_utf8(corrected.display_text);
    const std::string bytes = to_utf8(L"abc vi\u1EBFt ");
    const auto byte_span = vn_ime::FindVerifiedSmartUndoBytesBeforeCaret(
        bytes, bytes.length(), display_utf8, corrected);
    assert_true(byte_span.has_value() && byte_span->has_trailing_space,
                "Smart Undo verifies Scintilla UTF-8 span");

    vn_ime::SecureClearCommitUndoEntry(corrected);
    assert_true(!routes(corrected),
                "Consumed Smart Undo entry cannot route a second Backspace");

    for (const CorrectionLevel level : {
             CorrectionLevel::Normal,
             CorrectionLevel::Advanced,
             CorrectionLevel::Experimental}) {
        Engine engine(InputMethod::Telex);
        engine.SetCorrectionLevel(level);
        type_string(engine, level == CorrectionLevel::Normal
                                ? L"vies"
                                : L"dduowgnf");
        const EngineDisplayResult result = engine.GetDisplayResult();
        assert_true(result.HasSpellerCorrection() &&
                        result.correction_high_confidence,
                    "Enabled correction level reports Smart Undo metadata");
    }

    Engine correction_off(InputMethod::Telex);
    correction_off.SetCorrectionLevel(CorrectionLevel::Off);
    type_string(correction_off, L"vies");
    assert_true(!correction_off.GetDisplayResult().HasSpellerCorrection(),
                "Correction Off does not report Smart Undo metadata");

    Engine telex_conversion(InputMethod::Telex);
    type_string(telex_conversion, L"tees");
    assert_true(!telex_conversion.GetDisplayResult().HasSpellerCorrection(),
                "Normal Telex tone conversion is not a Smart Undo correction");

    Engine capitalization_only(InputMethod::Telex);
    type_string(capitalization_only, L"tees");
    assert_true(capitalization_only.UpdateCasingFromHost(L"T\u1EBF") &&
                    !capitalization_only.GetDisplayResult().HasSpellerCorrection(),
                "Host capitalization alone is not a Smart Undo correction");

    Engine vni_conversion(InputMethod::VNI);
    type_string(vni_conversion, L"te1");
    assert_true(!vni_conversion.GetDisplayResult().HasSpellerCorrection(),
                "Normal VNI tone conversion is not a Smart Undo correction");
}

void test_direct_inline_restore_span_verification() {
    std::cout << "\nRunning test_direct_inline_restore_span_verification..." << std::endl;

    {
        auto span = vn_ime::FindVerifiedTextBeforeCaret(L"abc viết", 8, L"viết");
        assert_true(span.has_value(), "Direct restore verifies matching wide text");
        assert_true(span && span->start == 4 && span->end == 8, "Direct restore wide span bounds");
    }
    {
        auto span = vn_ime::FindVerifiedTextBeforeCaret(L"abc viện", 8, L"viết");
        assert_true(!span.has_value(), "Direct restore rejects changed wide text");
    }
    {
        auto span = vn_ime::FindVerifiedTextBeforeCaret(L"abc viết", 3, L"viết");
        assert_true(!span.has_value(), "Direct restore rejects caret before display");
    }
    {
        const std::string text = to_utf8(L"abc viết");
        const std::string display = to_utf8(L"viết");
        auto span = vn_ime::FindVerifiedBytesBeforeCaret(text, text.length(), display);
        assert_true(span.has_value(), "Direct restore verifies matching UTF-8 text");
        assert_true(span && span->start == text.length() - display.length() && span->end == text.length(),
                    "Direct restore UTF-8 span bounds");
    }
    {
        const std::string text = to_utf8(L"abc viện");
        const std::string display = to_utf8(L"viết");
        auto span = vn_ime::FindVerifiedBytesBeforeCaret(text, text.length(), display);
        assert_true(!span.has_value(), "Direct restore rejects changed UTF-8 text");
    }
    {
        const std::wstring text = L"prefix tuttat1";
        const auto span = vn_ime::FindVerifiedTextBeforeCaret(
            text, text.length(), L"tuttat1");
        assert_true(
            span && span->start == 7 && span->end == text.length(),
            "Direct segmentation rewrite verifies exact UTF-16 host span");
        assert_true(
            !vn_ime::FindVerifiedTextBeforeCaret(
                L"prefix tuttat2", 14, L"tuttat1"),
            "Direct segmentation rewrite rejects UTF-16 host mismatch");
        assert_true(
            !vn_ime::FindVerifiedTextBeforeCaret(
                text, 5, L"tuttat1"),
            "Direct segmentation rewrite rejects insufficient UTF-16 caret");
    }
    {
        const std::string text = to_utf8(L"prefix tút tát");
        const std::string display = to_utf8(L"tút tát");
        const auto span = vn_ime::FindVerifiedBytesBeforeCaret(
            text, text.length(), display);
        assert_true(
            span && span->start == text.length() - display.length() &&
                span->end == text.length(),
            "Direct segmentation rewrite verifies exact UTF-8 byte span");
        assert_true(
            !vn_ime::FindVerifiedBytesBeforeCaret(
                text, display.length() - 1, display),
            "Direct segmentation rewrite rejects insufficient UTF-8 caret");
    }
}

void test_engine_correction_level_runtime() {
    std::cout << "\nRunning test_engine_correction_level_runtime..." << std::endl;

    {
        Engine engine(InputMethod::Telex);
        type_string(engine, L"tuaaf");
        assert_true(engine.GetDisplayString() != L"tuần", "Default Normal does not apply Advanced missing-final correction");
        assert_true(engine.GetCorrectionLevel() == CorrectionLevel::Normal, "Engine default correction level is Normal");
    }
    {
        Engine engine(InputMethod::Telex);
        engine.SetCorrectionLevel(CorrectionLevel::Advanced);
        type_string(engine, L"tuaaf");
        assert_eq(engine.GetDisplayString(), L"tuần", "Advanced runtime corrects tuaaf -> tuần");
    }
    {
        Engine engine(InputMethod::Telex);
        engine.SetCorrectionLevel(CorrectionLevel::Advanced);
        type_string(engine, L"dduowgnf");
        assert_eq(engine.GetDisplayString(), L"đường", "Advanced runtime corrects dduowgnf -> đường");
    }
    {
        Engine engine(InputMethod::Telex);
        engine.SetCorrectionLevel(CorrectionLevel::Experimental);
        type_string(engine, L"thuyeet");
        assert_eq(engine.GetDisplayString(), L"thuyết", "Experimental runtime aliases Advanced correction");
    }
    {
        Engine engine(InputMethod::Telex);
        engine.SetCorrectionLevel(CorrectionLevel::Off);
        type_string(engine, L"vies");
        assert_eq(engine.GetDisplayString(), L"víe", "Off disables Normal correction");
        assert_true(!engine.GetAutoCorrect(), "Off disables auto-correct compatibility getter");
    }
    {
        Engine engine(InputMethod::Telex);
        engine.SetAutoCorrect(false);
        assert_true(engine.GetCorrectionLevel() == CorrectionLevel::Off, "SetAutoCorrect(false) maps to Off");
        engine.SetAutoCorrect(true);
        assert_true(engine.GetCorrectionLevel() == CorrectionLevel::Normal, "SetAutoCorrect(true) restores Normal from Off");
    }
}

void test_vietnamese_syllable_validity() {
    std::cout << "\nRunning test_vietnamese_syllable_validity..." << std::endl;

    using vn_ime::core::rules::SyllableValidity;
    using vn_ime::core::rules::ValidateVietnameseSyllable;

    // Test ValidPrefix (consonants only)
    assert_true(ValidateVietnameseSyllable(L"h") == SyllableValidity::ValidPrefix, "h is a valid prefix");
    assert_true(ValidateVietnameseSyllable(L"th") == SyllableValidity::ValidPrefix, "th is a valid prefix");
    assert_true(ValidateVietnameseSyllable(L"tr") == SyllableValidity::ValidPrefix, "tr is a valid prefix");
    assert_true(ValidateVietnameseSyllable(L"ngh") == SyllableValidity::ValidPrefix, "ngh is a valid prefix");

    // Test ValidPrefix (in-progress vowels)
    assert_true(ValidateVietnameseSyllable(L"viet") == SyllableValidity::ValidPrefix, "viet lacks tone for stop consonant");
    assert_true(ValidateVietnameseSyllable(L"duoc") == SyllableValidity::ValidPrefix, "duoc lacks tone for stop consonant");
    assert_true(ValidateVietnameseSyllable(L"tuye") == SyllableValidity::ValidPrefix, "tuye is in-progress vowel");
    assert_true(ValidateVietnameseSyllable(L"uo") == SyllableValidity::ValidPrefix, "uo is in-progress vowel");

    // Test Valid
    assert_true(ValidateVietnameseSyllable(L"viết") == SyllableValidity::Valid, "viết is a complete syllable");
    assert_true(ValidateVietnameseSyllable(L"được") == SyllableValidity::Valid, "được is a complete syllable");
    assert_true(ValidateVietnameseSyllable(L"anh") == SyllableValidity::Valid, "anh is a complete syllable");
    assert_true(ValidateVietnameseSyllable(L"hoàng") == SyllableValidity::Valid, "hoàng is a complete syllable");
    assert_true(ValidateVietnameseSyllable(L"a") == SyllableValidity::Valid, "a is a complete syllable");

    // Test Invalid
    assert_true(ValidateVietnameseSyllable(L"ănh") == SyllableValidity::Invalid, "ănh is invalid phonotactically");
    assert_true(ValidateVietnameseSyllable(L"github") == SyllableValidity::Invalid, "github is not Vietnamese");
    assert_true(ValidateVietnameseSyllable(L"qtr") == SyllableValidity::Invalid, "qtr is not Vietnamese");
    assert_true(ValidateVietnameseSyllable(L"") == SyllableValidity::Invalid, "empty is invalid");
}

void test_speller_ex_candidates() {
    std::cout << "\nRunning test_speller_ex_candidates..." << std::endl;

    using namespace vn_ime::core::speller;

    {
        vn_ime::core::Engine engine(InputMethod::Telex);
        engine.ProcessKey(L'c');
        engine.ProcessKey(L'o');
        engine.ProcessKey(L'd');
        engine.ProcessKey(L'e');
        CorrectionResult resN = CorrectWordEx(engine.GetDisplayString(), L"code", CorrectionLevel::Normal, InputMethod::Telex);
        CorrectionResult resA = CorrectWordEx(engine.GetDisplayString(), L"code", CorrectionLevel::Advanced, InputMethod::Telex);
        CorrectionResult resE = CorrectWordEx(engine.GetDisplayString(), L"code", CorrectionLevel::Experimental, InputMethod::Telex);
        assert_true(!resN.changed && resN.word == L"code", "code unchanged under Normal");
        assert_true(!resE.changed && resE.word == L"code", "code unchanged under Experimental");
    }

    // L"vies" -> L"viết" (MissingFinalT)
    {
        CorrectionResult res = CorrectWordEx(L"vies", L"vies", CorrectionLevel::Normal);
        assert_true(res.changed, "vies changed is true");
        assert_true(res.word == L"viết", "vies corrected word is viết");
        assert_true(res.kind == CorrectionKind::MissingFinalT, "vies kind is MissingFinalT");
        assert_true(res.score == 900, "vies score is 900");
    }

    // L"đuọc" -> L"được" (UoVowelSubstitution)
    {
        CorrectionResult res = CorrectWordEx(L"đuọc", L"dduocj", CorrectionLevel::Normal);
        assert_true(res.changed, "dduocj changed is true");
        assert_true(res.word == L"được", "dduocj corrected word is được");
        assert_true(res.kind == CorrectionKind::UoVowelSubstitution, "dduocj kind is UoVowelSubstitution");
        assert_true(res.score == 900, "dduocj score is 900");
    }

    // Telex: L"tuyetn" -> L"tuyền" (AdjacentKeySwap)
    {
        CorrectionResult res = CorrectWordEx(L"tuyetn", L"tuyetn", CorrectionLevel::Normal, InputMethod::Telex);
        assert_true(res.changed, "Telex tuyetn changed is true");
        assert_true(res.word == L"tuyền", "Telex tuyetn corrected word is tuyền");
        assert_true(res.kind == CorrectionKind::AdjacentKeySwap, "Telex tuyetn kind is AdjacentKeySwap");
        assert_true(res.score == 900, "Telex tuyetn score is 900");
    }

    // Telex: explicit whitelist maps known ...tn typos through nearby tone key f.
    {
        CorrectionResult res = CorrectWordEx(L"vietn", L"vietn", CorrectionLevel::Normal, InputMethod::Telex);
        assert_true(res.changed, "Telex vietn changed is true");
        assert_true(res.word == L"vi\u1EC1n", "Telex vietn corrected word is vi\u1EC1n");
        assert_true(res.kind == CorrectionKind::AdjacentKeySwap, "Telex vietn kind is AdjacentKeySwap");
        assert_true(res.score == 900, "Telex vietn score is 900");
    }
    {
        CorrectionResult res = CorrectWordEx(L"thietn", L"thietn", CorrectionLevel::Normal, InputMethod::Telex);
        assert_true(res.changed, "Telex thietn changed is true");
        assert_true(res.word == L"thi\u1EC1n", "Telex thietn corrected word is thi\u1EC1n");
        assert_true(res.kind == CorrectionKind::AdjacentKeySwap, "Telex thietn kind is AdjacentKeySwap");
        assert_true(res.score == 900, "Telex thietn score is 900");
    }
    {
        CorrectionResult res = CorrectWordEx(L"kietn", L"kietn", CorrectionLevel::Normal, InputMethod::Telex);
        assert_true(!res.changed, "Telex kietn changed is false");
        assert_true(res.word == L"kietn", "Telex kietn word stays raw");
    }

    // VNI: same explicit whitelist maps these known raw typos to the huyền target family.
    {
        CorrectionResult res = CorrectWordEx(L"tuyetn", L"tuyetn", CorrectionLevel::Normal, InputMethod::VNI);
        assert_true(res.changed, "VNI tuyetn changed is true");
        assert_true(res.word == L"tuy\u1EC1n", "VNI tuyetn corrected word is tuy\u1EC1n");
        assert_true(res.kind == CorrectionKind::AdjacentKeySwap, "VNI tuyetn kind is AdjacentKeySwap");
        assert_true(res.score == 900, "VNI tuyetn score is 900");
    }
    {
        CorrectionResult res = CorrectWordEx(L"vietn", L"vietn", CorrectionLevel::Normal, InputMethod::VNI);
        assert_true(res.changed, "VNI vietn changed is true");
        assert_true(res.word == L"vi\u1EC1n", "VNI vietn corrected word is vi\u1EC1n");
        assert_true(res.kind == CorrectionKind::AdjacentKeySwap, "VNI vietn kind is AdjacentKeySwap");
        assert_true(res.score == 900, "VNI vietn score is 900");
    }
    {
        CorrectionResult res = CorrectWordEx(L"thietn", L"thietn", CorrectionLevel::Normal, InputMethod::VNI);
        assert_true(res.changed, "VNI thietn changed is true");
        assert_true(res.word == L"thi\u1EC1n", "VNI thietn corrected word is thi\u1EC1n");
        assert_true(res.kind == CorrectionKind::AdjacentKeySwap, "VNI thietn kind is AdjacentKeySwap");
        assert_true(res.score == 900, "VNI thietn score is 900");
    }
    {
        CorrectionResult res = CorrectWordEx(L"kietn", L"kietn", CorrectionLevel::Normal, InputMethod::VNI);
        assert_true(!res.changed, "VNI kietn changed is false");
        assert_true(res.word == L"kietn", "VNI kietn word stays raw");
    }

    // L"hòa" -> L"hoà" (ToneRelocation)
    {
        CorrectionResult res = CorrectWordEx(L"hòa", L"hoaf", CorrectionLevel::Normal);
        assert_true(res.changed, "hòa changed is true");
        assert_true(res.word == L"hoà", "hòa corrected word is hoà");
        assert_true(res.kind == CorrectionKind::ToneRelocation, "hòa kind is ToneRelocation");
        assert_true(res.score == 900, "hòa score is 900");
    }

    // L"github" -> None
    {
        CorrectionResult res = CorrectWordEx(L"github", L"github", CorrectionLevel::Normal);
        assert_true(!res.changed, "github changed is false");
        assert_true(res.kind == CorrectionKind::None, "github kind is None");
        assert_true(res.score == 0, "github score is 0");
    }

    // Calling with CorrectionLevel::Off -> changed = false
    {
        CorrectionResult res = CorrectWordEx(L"vies", L"vies", CorrectionLevel::Off);
        assert_true(!res.changed, "vies with Off changed is false");
        assert_true(res.kind == CorrectionKind::None, "vies with Off kind is None");
        assert_true(res.score == 0, "vies with Off score is 0");
    }

    // Telex Off preserves raw word
    {
        CorrectionResult res = CorrectWordEx(L"tuyetn", L"tuyetn", CorrectionLevel::Off, InputMethod::Telex);
        assert_true(!res.changed, "Telex tuyetn with Off changed is false");
        assert_true(res.word == L"tuyetn", "Telex tuyetn with Off word stays tuyetn");
    }

    // VNI Off preserves raw word
    {
        CorrectionResult res = CorrectWordEx(L"tuyetn", L"tuyetn", CorrectionLevel::Off, InputMethod::VNI);
        assert_true(!res.changed, "VNI tuyetn with Off changed is false");
        assert_true(res.word == L"tuyetn", "VNI tuyetn with Off word stays tuyetn");
    }

    // Missing Modifier: kiẻm -> kiểm
    {
        CorrectionResult res = CorrectWordEx(L"kiẻm", L"kiemr", CorrectionLevel::Normal);
        assert_true(res.changed, "kiẻm changed is true");
        assert_true(res.word == L"kiểm", "kiẻm corrected word is kiểm");
        assert_true(res.kind == CorrectionKind::MissingModifier, "kiẻm kind is MissingModifier");
        assert_true(res.score == 900, "kiẻm score is 900");
    }

    // Missing Modifier: kiém -> kiếm
    {
        CorrectionResult res = CorrectWordEx(L"kiém", L"kiems", CorrectionLevel::Normal);
        assert_true(res.changed, "kiém changed is true");
        assert_true(res.word == L"kiếm", "kiém corrected word is kiếm");
        assert_true(res.kind == CorrectionKind::MissingModifier, "kiém kind is MissingModifier");
        assert_true(res.score == 900, "kiém score is 900");
    }

    // Missing Modifier: kiẹm -> kiệm
    {
        CorrectionResult res = CorrectWordEx(L"kiẹm", L"kiemj", CorrectionLevel::Normal);
        assert_true(res.changed, "kiẹm changed is true");
        assert_true(res.word == L"kiệm", "kiẹm corrected word is kiệm");
        assert_true(res.kind == CorrectionKind::MissingModifier, "kiẹm kind is MissingModifier");
        assert_true(res.score == 900, "kiẹm score is 900");
    }

    // Modifier/Tone Before Vowel: VNI v6ay5 -> vậy (VNI outputs v6ạy)
    {
        CorrectionResult res = CorrectWordEx(L"v6ạy", L"v6ay5", CorrectionLevel::Normal, InputMethod::VNI);
        assert_true(res.changed, "v6ạy changed is true");
        assert_true(res.word == L"vậy", "v6ạy corrected word is vậy");
        assert_true(res.kind == CorrectionKind::AdjacentKeySwap, "v6ạy kind is AdjacentKeySwap");
        assert_true(res.score == 900, "v6ạy score is 900");
    }

    // Modifier/Tone Before Vowel: Telex vwatj -> vặt (Telex outputs vwạt)
    {
        CorrectionResult res = CorrectWordEx(L"vwạt", L"vwatj", CorrectionLevel::Normal, InputMethod::Telex);
        assert_true(res.changed, "vwatj changed is true");
        assert_true(res.word == L"vặt", "vwatj corrected word is vặt");
        assert_true(res.kind == CorrectionKind::AdjacentKeySwap, "vwatj kind is AdjacentKeySwap");
        assert_true(res.score == 900, "vwatj score is 900");
    }
}

void test_advanced_correction_candidates() {
    std::cout << "\nRunning test_advanced_correction_candidates..." << std::endl;

    using namespace vn_ime::core::speller;

    // Missing Consonant: L"tuầ" -> L"tuần"
    {
        CorrectionResult res = CorrectWordEx(L"tuầ", L"tuaf", CorrectionLevel::Advanced);
        assert_true(res.changed, "tuầ changed is true");
        assert_true(res.word == L"tuần", "tuầ corrected word is tuần");
        assert_true(res.kind == CorrectionKind::MissingFinalT, "tuầ kind is MissingFinalT");
        assert_true(res.score == 900, "tuầ score is 900");
    }

    // Missing Consonant: level gating
    {
        CorrectionResult res = CorrectWordEx(L"tuầ", L"tuaf", CorrectionLevel::Normal);
        assert_true(!res.changed, "tuầ with Normal changed is false");
    }

    // Adjacent Final Key Swap: L"đườgn" -> L"đường"
    {
        CorrectionResult res = CorrectWordEx(L"đườgn", L"dduowgnf", CorrectionLevel::Advanced);
        assert_true(res.changed, "đườgn changed is true");
        assert_true(res.word == L"đường", "đườgn corrected word is đường");
        assert_true(res.kind == CorrectionKind::AdjacentKeySwap, "đườgn kind is AdjacentKeySwap");
        assert_true(res.score == 900, "đườgn score is 900");
    }

    // Adjacent Final Key Swap: level gating
    {
        CorrectionResult res = CorrectWordEx(L"đườgn", L"dduowgnf", CorrectionLevel::Normal);
        assert_true(!res.changed, "đườgn with Normal changed is false");
    }

    // Missing Tone: L"thuyêt" -> L"thuyết"
    {
        CorrectionResult res = CorrectWordEx(L"thuyêt", L"thuyet", CorrectionLevel::Advanced);
        assert_true(res.changed, "thuyêt changed is true");
        assert_true(res.word == L"thuyết", "thuyêt corrected word is thuyết");
        assert_true(res.kind == CorrectionKind::MissingTone, "thuyêt kind is MissingTone");
        assert_true(res.score == 900, "thuyêt score is 900");
    }

    // Missing Tone: level gating
    {
        CorrectionResult res = CorrectWordEx(L"thuyêt", L"thuyet", CorrectionLevel::Normal);
        assert_true(!res.changed, "thuyêt with Normal changed is false");
    }

    // Missing Tone: L"luât" -> L"luật"
    {
        CorrectionResult res = CorrectWordEx(L"luât", L"luat", CorrectionLevel::Advanced);
        assert_true(res.changed, "luât changed is true");
        assert_true(res.word == L"luật", "luât corrected word is luật");
        assert_true(res.kind == CorrectionKind::MissingTone, "luât kind is MissingTone");
        assert_true(res.score == 900, "luât score is 900");
    }

    // Telex: L"vaw" -> L"vá" (Advanced adjacent correction)
    {
        CorrectionResult res = CorrectWordEx(L"vaw", L"vaw", CorrectionLevel::Advanced, InputMethod::Telex);
        assert_true(res.changed, "Telex vaw changed is true under Advanced");
        assert_true(res.word == L"vá", "Telex vaw corrected word is vá");
        assert_true(res.kind == CorrectionKind::AdjacentKeySwap, "Telex vaw kind is AdjacentKeySwap");
    }
    // Telex: L"vae" -> L"vả" (Advanced adjacent correction)
    {
        CorrectionResult res = CorrectWordEx(L"vae", L"vae", CorrectionLevel::Advanced, InputMethod::Telex);
        assert_true(res.changed, "Telex vae changed is true under Advanced");
        assert_true(res.word == L"vả", "Telex vae corrected word is vả");
        assert_true(res.kind == CorrectionKind::AdjacentKeySwap, "Telex vae kind is AdjacentKeySwap");
    }

    // VNI: L"ver" -> L"vẽ" (Advanced adjacent correction)
    {
        CorrectionResult res = CorrectWordEx(L"ver", L"ver", CorrectionLevel::Advanced, InputMethod::VNI);
        assert_true(res.changed, "VNI ver changed is true under Advanced");
        assert_true(res.word == L"vẽ", "VNI ver corrected word is vẽ");
        assert_true(res.kind == CorrectionKind::AdjacentKeySwap, "VNI ver kind is AdjacentKeySwap");
    }

    // VNI: L"vern" -> L"vẹn" (Advanced adjacent correction in the middle)
    {
        CorrectionResult res = CorrectWordEx(L"vern", L"vern", CorrectionLevel::Advanced, InputMethod::VNI);
        assert_true(res.changed, "VNI vern changed is true under Advanced");
        assert_true(res.word == L"vẹn", "VNI vern corrected word is vẹn");
        assert_true(res.kind == CorrectionKind::AdjacentKeySwap, "VNI vern kind is AdjacentKeySwap");
    }

    // VNI: L"vetn" -> L"vẹn" (Advanced adjacent correction in the middle)
    {
        CorrectionResult res = CorrectWordEx(L"vetn", L"vetn", CorrectionLevel::Advanced, InputMethod::VNI);
        assert_true(res.changed, "VNI vetn changed is true under Advanced");
        assert_true(res.word == L"vẹn", "VNI vetn corrected word is vẹn");
        assert_true(res.kind == CorrectionKind::AdjacentKeySwap, "VNI vetn kind is AdjacentKeySwap");
    }
}

void test_advanced_negative_cases() {
    std::cout << "\nRunning test_advanced_negative_cases..." << std::endl;

    using namespace vn_ime::core::speller;

    // Ambiguous missing consonant (multiple dictionary matches): L"tíê" with raw "tief"
    // tie + n = tiến, tie + p = tiếp, tie + t = tiết, tie + m = tiếm, etc.
    {
        CorrectionResult res = CorrectWordEx(L"tíê", L"tief", CorrectionLevel::Advanced);
        assert_true(!res.changed, "tíê has multiple candidates, changed is false");
    }

    // Valid word like L"hoãng" remains unchanged under Advanced
    {
        CorrectionResult res = CorrectWordEx(L"hoãng", L"hoangx", CorrectionLevel::Advanced);
        assert_true(!res.changed, "hoãng is valid, changed is false");
    }

    // Non-Vietnamese word like L"github" remains unchanged under Advanced
    {
        CorrectionResult res = CorrectWordEx(L"github", L"github", CorrectionLevel::Advanced);
        assert_true(!res.changed, "github remains unchanged under Advanced");
    }

    // Ambiguous VNI adjacent key correction: L"vaq" with raw "vaq" stays "vaq"
    // vaq could be va1 (vá) or va2 (và)
    {
        CorrectionResult res = CorrectWordEx(L"vaq", L"vaq", CorrectionLevel::Advanced, InputMethod::VNI);
        assert_true(!res.changed, "VNI vaq remains unchanged (ambiguous)");
    }
    {
        // 1. New-style "khỏe" (tone on e: U+006B U+0068 U+006F U+1EBB)
        std::wstring new_khoe = L"kh\u006F\u1EBBe";
        CorrectionResult res = CorrectWordEx(new_khoe, L"khoer", CorrectionLevel::Advanced, InputMethod::Telex);
        assert_true(!res.changed, "New-style khỏe remains unchanged");
    }
    {
        // 2. Old-style "khoẻ" (tone on o: U+006B U+0068 U+1ECF U+0065)
        std::wstring old_khoe = L"kh\u1ECFe";
        CorrectionResult res = CorrectWordEx(old_khoe, L"khoer", CorrectionLevel::Advanced, InputMethod::Telex);
        assert_true(!res.changed, "Old-style khoẻ remains unchanged");
    }
    {
        // 3. New-style "khoé" (tone on e: U+006B U+0068 U+006F U+00E9)
        std::wstring new_khoe = L"kh\u006F\u00E9";
        CorrectionResult res = CorrectWordEx(new_khoe, L"khoes", CorrectionLevel::Advanced, InputMethod::Telex);
        assert_true(!res.changed, "New-style khoé remains unchanged");
    }
    {
        // 4. Old-style "khóe" (tone on o: U+006B U+0068 U+00F3 U+0065)
        std::wstring old_khoe = L"kh\u00F3e";
        CorrectionResult res = CorrectWordEx(old_khoe, L"khoes", CorrectionLevel::Advanced, InputMethod::Telex);
        assert_true(!res.changed, "Old-style khóe remains unchanged");
    }
    {
        Engine engine(InputMethod::Telex);
        engine.SetCorrectionLevel(CorrectionLevel::Advanced);
        type_string(engine, L"khoer");
        std::wstring result = engine.GetDisplayString();
        assert_true(result == L"kh\u006F\u1EBBe" || result == L"kh\u1ECFe", "Engine typed khoer does not get corrected to khỏ");
    }
    {
        Engine engine(InputMethod::Telex);
        engine.SetCorrectionLevel(CorrectionLevel::Advanced);
        type_string(engine, L"khore");
        std::wstring result = engine.GetDisplayString();
        assert_true(result == L"kh\u006F\u1EBBe" || result == L"kh\u1ECFe", "Engine typed khore does not get corrected to khỏ");
    }
}

void test_realtime_modifier_tone_before_vowel() {
    std::cout << "\nRunning test_realtime_modifier_tone_before_vowel..." << std::endl;

    // Telex cases
    {
        Engine engine(InputMethod::Telex);
        engine.SetAutoCorrect(false); // disable speller to test pure engine behavior
        
        type_string(engine, L"vwatj");
        assert_eq(engine.GetDisplayString(), L"vặt", "Telex realtime vwatj -> vặt");
    }
    {
        Engine engine(InputMethod::Telex);
        engine.SetAutoCorrect(false);
        
        type_string(engine, L"vwt");
        assert_eq(engine.GetDisplayString(), L"vưt", "Telex realtime vwt -> vưt");
    }
    {
        Engine engine(InputMethod::Telex);
        engine.SetAutoCorrect(false);
        
        type_string(engine, L"vws");
        assert_eq(engine.GetDisplayString(), L"vứ", "Telex realtime vws -> vứ");
    }
    {
        Engine engine(InputMethod::Telex);
        engine.SetAutoCorrect(false);
        
        type_string(engine, L"vwat");
        assert_eq(engine.GetDisplayString(), L"văt", "Telex realtime vwat -> văt");
    }

    // VNI cases
    {
        Engine engine(InputMethod::VNI);
        engine.SetAutoCorrect(false);
        
        type_string(engine, L"v6ay5");
        assert_eq(engine.GetDisplayString(), L"vậy", "VNI realtime v6ay5 -> vậy");
    }
    {
        Engine engine(InputMethod::VNI);
        engine.SetAutoCorrect(false);
        
        type_string(engine, L"v6t");
        assert_eq(engine.GetDisplayString(), L"v6t", "VNI realtime v6t -> v6t (literal flush)");
    }
    {
        Engine engine(InputMethod::VNI);
        engine.SetAutoCorrect(false);
        
        type_string(engine, L"v7e");
        assert_eq(engine.GetDisplayString(), L"v7e", "VNI realtime v7e -> v7e (incompatible, literal flush)");
    }
    {
        Engine engine(InputMethod::VNI);
        engine.SetAutoCorrect(false);
        
        type_string(engine, L"v1a");
        assert_eq(engine.GetDisplayString(), L"vá", "VNI realtime v1a -> vá");
    }
    {
        Engine engine(InputMethod::VNI);
        engine.SetAutoCorrect(false);
        
        type_string(engine, L"2a");
        assert_eq(engine.GetDisplayString(), L"2a", "VNI realtime 2a -> 2a (starts with digit bypass)");
    }
}

void test_redundant_horn_key_dropping_for_uy() {
    std::cout << "\nRunning test_redundant_horn_key_dropping_for_uy..." << std::endl;

    // Telex tests under default Normal correction level
    {
        Engine engine(InputMethod::Telex);
        engine.SetCorrectionLevel(CorrectionLevel::Normal);
        type_string(engine, L"uyewe");
        assert_eq(engine.GetDisplayString(), L"uyê", "Telex uyewe -> uyê (redundant w dropped)");
    }
    {
        Engine engine(InputMethod::Telex);
        engine.SetCorrectionLevel(CorrectionLevel::Normal);
        type_string(engine, L"uyewen");
        assert_eq(engine.GetDisplayString(), L"uyên", "Telex uyewen -> uyên (redundant w dropped)");
    }
    {
        Engine engine(InputMethod::Telex);
        engine.SetCorrectionLevel(CorrectionLevel::Normal);
        type_string(engine, L"uyew");
        assert_eq(engine.GetDisplayString(), L"uye", "Telex uyew -> uye (redundant w dropped)");
    }
    
    // Telex test under Off level (no dropping)
    {
        Engine engine(InputMethod::Telex);
        engine.SetCorrectionLevel(CorrectionLevel::Off);
        type_string(engine, L"uyew");
        assert_eq(engine.GetDisplayString(), L"ưye", "Telex uyew -> ưye under Off level");
    }

    // VNI tests under default Normal correction level
    {
        Engine engine(InputMethod::VNI);
        engine.SetCorrectionLevel(CorrectionLevel::Normal);
        type_string(engine, L"uye67n");
        assert_eq(engine.GetDisplayString(), L"uyên", "VNI uye67n -> uyên (redundant 7 dropped)");
    }
    {
        Engine engine(InputMethod::VNI);
        engine.SetCorrectionLevel(CorrectionLevel::Normal);
        type_string(engine, L"uye76n");
        assert_eq(engine.GetDisplayString(), L"uyên", "VNI uye76n -> uyên (redundant 7 dropped)");
    }
    {
        Engine engine(InputMethod::VNI);
        engine.SetCorrectionLevel(CorrectionLevel::Normal);
        type_string(engine, L"uye7");
        assert_eq(engine.GetDisplayString(), L"uye", "VNI uye7 -> uye (redundant 7 dropped)");
    }

    // VNI test under Off level (no dropping)
    {
        Engine engine(InputMethod::VNI);
        engine.SetCorrectionLevel(CorrectionLevel::Off);
        type_string(engine, L"uye7");
        assert_eq(engine.GetDisplayString(), L"ưye", "VNI uye7 -> ưye under Off level");
    }
}

void test_stale_modifier_override_correction() {
    std::cout << "\nRunning test_stale_modifier_override_correction..." << std::endl;

    {
        Engine engine(InputMethod::VNI);
        type_string(engine, L"ho7a8");
        assert_eq(engine.GetDisplayString(), L"hoă",
                  "VNI Normal keeps latest 8: ho7a8 -> hoă");
        assert_eq(engine.GetRawString(), L"ho7a8",
                  "VNI stale modifier correction preserves raw keys");

        engine.Backspace();
        assert_eq(engine.GetRawString(), L"ho7a",
                  "VNI stale modifier Backspace removes only latest raw key");
        assert_eq(engine.GetDisplayString(), L"hơa",
                  "VNI stale modifier Backspace restores the remaining 7 effect");
        engine.ProcessKey(L'8');
        assert_eq(engine.GetDisplayString(), L"hoă",
                  "VNI stale modifier correction reapplies after retyping 8");
    }

    assert_engine_output(InputMethod::VNI, L"ho7ac8", L"hoăc",
                         "VNI stale horn before post-coda 8 -> hoăc");
    assert_engine_output(InputMethod::VNI, L"ho7ac85", L"hoặc",
                         "VNI ho7ac85 -> hoặc");
    assert_engine_output(InputMethod::VNI, L"ho7ac58", L"hoặc",
                         "VNI stale horn correction supports tone before 8");
    assert_engine_output(InputMethod::VNI, L"ho6ac85", L"hoặc",
                         "VNI stale circumflex before 8 -> hoặc");
    assert_engine_output(InputMethod::VNI, L"Ho7ac85", L"Hoặc",
                         "VNI stale modifier correction preserves title case");

    {
        Engine engine(InputMethod::VNI);
        engine.SetCorrectionLevel(CorrectionLevel::Off);
        type_string(engine, L"ho7a8");
        assert_eq(engine.GetDisplayString(), L"hơă",
                  "VNI Off does not remove the stale modifier");
    }
    for (const CorrectionLevel level : {
             CorrectionLevel::Advanced,
             CorrectionLevel::Experimental}) {
        Engine engine(InputMethod::VNI);
        engine.SetCorrectionLevel(level);
        type_string(engine, L"ho7ac85");
        assert_eq(engine.GetDisplayString(), L"hoặc",
                  "VNI Advanced/Experimental inherits stale modifier correction");
    }

    assert_engine_output(InputMethod::Telex, L"hoaw", L"hoă",
                         "Telex oa+w targets breve on a");
    assert_engine_output(InputMethod::Telex, L"hoawcj", L"hoặc",
                         "Telex canonical hoawcj -> hoặc");
    assert_engine_output(InputMethod::Telex, L"hoacwj", L"hoặc",
                         "Telex post-coda w in hoacwj -> hoặc");
    assert_engine_output(InputMethod::Telex, L"howaw", L"hoă",
                         "Telex keeps latest w: howaw -> hoă");
    assert_engine_output(InputMethod::Telex, L"howawcj", L"hoặc",
                         "Telex howawcj -> hoặc");
    assert_engine_output(InputMethod::Telex, L"howacwj", L"hoặc",
                         "Telex stale w supports post-coda modifier");
    assert_engine_output(InputMethod::Telex, L"hooacwj", L"hoặc",
                         "Telex stale oo modifier is removed before latest w");
    assert_engine_output(InputMethod::Telex, L"aaw", L"ă",
                         "Telex latest w overrides stale aa modifier");
    assert_engine_output(InputMethod::SimpleTelex, L"howacwj", L"hoặc",
                         "SimpleTelex inherits stale modifier correction");

    {
        Engine engine(InputMethod::Telex);
        engine.SetCorrectionLevel(CorrectionLevel::Off);
        type_string(engine, L"howaw");
        assert_eq(engine.GetDisplayString(), L"hơă",
                  "Telex Off does not remove the stale modifier");
    }
    for (const CorrectionLevel level : {
             CorrectionLevel::Advanced,
             CorrectionLevel::Experimental}) {
        Engine engine(InputMethod::Telex);
        engine.SetCorrectionLevel(level);
        type_string(engine, L"howacwj");
        assert_eq(engine.GetDisplayString(), L"hoặc",
                  "Telex Advanced/Experimental inherits stale modifier correction");
    }

    {
        const auto result = speller::CorrectWordEx(
            L"hơă", L"ho7a8", CorrectionLevel::Normal,
            InputMethod::VNI);
        assert_true(result.changed,
                    "Stale modifier correction reports a changed candidate");
        assert_eq(result.word, L"hoă",
                  "Stale modifier correction candidate is hoă");
        assert_true(
            result.kind == speller::CorrectionKind::StaleModifierOverride,
            "Stale modifier correction reports its dedicated kind");
        assert_true(result.high_confidence,
                    "Stale modifier correction is high confidence");
    }
    {
        const auto result = speller::CorrectWordEx(
            L"hơă", L"ho7a8a7a8a7a8", CorrectionLevel::Normal,
            InputMethod::VNI);
        assert_true(
            result.kind != speller::CorrectionKind::StaleModifierOverride,
            "Stale modifier correction rejects excessive modifier events");
    }

    assert_engine_output(InputMethod::Telex, L"uow", L"ươ",
                         "Telex valid uow horn pair remains unchanged");
    assert_engine_output(InputMethod::Telex, L"aww", L"aw",
                         "Telex ww escape after a remains unchanged");
    assert_engine_output(InputMethod::Telex, L"uww", L"uw",
                         "Telex ww escape after u remains unchanged");
    assert_engine_output(InputMethod::VNI, L"a68", L"ă",
                         "VNI same-vowel a68 override remains unchanged");
    assert_engine_output(InputMethod::VNI, L"a86", L"â",
                         "VNI same-vowel a86 override remains unchanged");
    assert_engine_output(InputMethod::VNI, L"thuo7c65", L"thuộc",
                         "VNI valid multi-modifier sequence remains unchanged");
    assert_engine_output(InputMethod::VNI, L"u77", L"u7",
                         "VNI doubled modifier escape remains unchanged");

    for (const std::wstring_view english : {
             L"power", L"hardware", L"download", L"workflow"}) {
        assert_engine_output(InputMethod::Telex, english,
                             std::wstring(english),
                             "English word remains protected from modifier recovery");
    }
}

void test_auto_word_segmentation_candidates() {
    std::cout << "\nRunning test_auto_word_segmentation_candidates..." << std::endl;

    auto build_from_engine = [](
        InputMethod method,
        std::wstring_view raw,
        CorrectionLevel level = CorrectionLevel::Experimental) {
        Engine engine(method);
        engine.SetCorrectionLevel(level);
        engine.SetEnglishProtectionLevel(EnglishProtectionLevel::Off);
        engine.SetSmartContextProtection(false);
        type_string(engine, raw);
        return speller::BuildAutoWordSegmentationCandidate(
            raw, engine.GetDisplayString(), method, level);
    };

    const auto vni = build_from_engine(InputMethod::VNI, L"tuttat1");
    assert_true(vni.has_value() && vni->high_confidence &&
                    vni->score >= 1500 && vni->runner_up_score == 0,
                "VNI attached token produces one high-confidence candidate");
    if (vni) {
        assert_eq(vni->text, L"t\u00FAt t\u00E1t",
                  "VNI tuttat1 segments to t\u00FAt t\u00E1t");
    }

    const auto telex = build_from_engine(InputMethod::Telex, L"tuttats");
    assert_true(telex.has_value(),
                "Telex attached token produces a segmentation candidate");
    if (telex) {
        assert_eq(telex->text, L"t\u00FAt t\u00E1t",
                  "Telex tuttats segments to t\u00FAt t\u00E1t");
    }

    const auto simple_telex =
        build_from_engine(InputMethod::SimpleTelex, L"tuttats");
    assert_true(simple_telex.has_value(),
                "Simple Telex shares method-aware segmentation evidence");

    const auto vni_per_syllable =
        speller::BuildAutoWordSegmentationCandidate(
            L"hoang2hon6", L"hoang2hon6", InputMethod::VNI,
            CorrectionLevel::Experimental);
    assert_true(vni_per_syllable.has_value(),
                "VNI per-syllable evidence produces a segmentation candidate");
    if (vni_per_syllable) {
        assert_eq(vni_per_syllable->text, L"ho\u00E0ng h\u00F4n",
                  "VNI hoang2hon6 segments to ho\u00E0ng h\u00F4n");
    }

    const auto telex_per_syllable =
        speller::BuildAutoWordSegmentationCandidate(
            L"hoangfhoon", L"hoangfhoon", InputMethod::Telex,
            CorrectionLevel::Experimental);
    assert_true(telex_per_syllable.has_value(),
                "Telex per-syllable evidence produces a segmentation candidate");
    if (telex_per_syllable) {
        assert_eq(telex_per_syllable->text, L"ho\u00E0ng h\u00F4n",
                  "Telex hoangfhoon segments to ho\u00E0ng h\u00F4n");
    }

    struct DynamicBigramCase {
        InputMethod method;
        std::wstring_view raw;
        std::wstring_view expected;
    };
    for (const DynamicBigramCase& test_case : {
             DynamicBigramCase{
                 InputMethod::VNI, L"may1tinh1",
                 L"m\u00E1y t\u00EDnh"},
             DynamicBigramCase{
                 InputMethod::Telex, L"maystinhs",
                 L"m\u00E1y t\u00EDnh"},
             DynamicBigramCase{
                 InputMethod::VNI, L"phan62mem62",
                 L"ph\u1EA7n m\u1EC1m"},
             DynamicBigramCase{
                 InputMethod::Telex, L"phaanfmeemf",
                 L"ph\u1EA7n m\u1EC1m"},
             DynamicBigramCase{
                 InputMethod::VNI, L"viet65nam",
                 L"Vi\u1EC7t Nam"},
             DynamicBigramCase{
                 InputMethod::Telex, L"vieetjnam",
                 L"Vi\u1EC7t Nam"},
             DynamicBigramCase{
                 InputMethod::VNI, L"kie63mtra",
                 L"ki\u1EC3m tra"},
             DynamicBigramCase{
                 InputMethod::Telex, L"kieemrtra",
                 L"ki\u1EC3m tra"},
             DynamicBigramCase{
                 InputMethod::VNI, L"kinhnghie65m",
                 L"kinh nghi\u1EC7m"},
             DynamicBigramCase{
                 InputMethod::Telex, L"kinhnghieemj",
                 L"kinh nghi\u1EC7m"},
             DynamicBigramCase{
                 InputMethod::VNI, L"phongphu1",
                 L"phong ph\u00FA"},
             DynamicBigramCase{
                 InputMethod::Telex, L"phongphus",
                 L"phong ph\u00FA"},
         }) {
        const auto candidate = build_from_engine(
            test_case.method, test_case.raw);
        assert_true(
            candidate.has_value(),
            "Dynamic split finds a curated two-word bigram");
        if (candidate) {
            assert_eq(
                candidate->text, std::wstring(test_case.expected),
                "Dynamic split replays both words with method-specific keys");
        }
    }

    assert_true(
        speller::BuildAutoWordSegmentationCandidate(
            L"tuttat1", L"tuttat1", InputMethod::VNI,
            CorrectionLevel::Experimental).has_value() &&
        speller::BuildAutoWordSegmentationCandidate(
            L"tuttats", L"tuttats", InputMethod::Telex,
            CorrectionLevel::Experimental).has_value() &&
        speller::BuildAutoWordSegmentationCandidate(
            L"tuttats", L"tuttats", InputMethod::SimpleTelex,
            CorrectionLevel::Experimental).has_value(),
        "Builder derives evidence when the runtime display is raw literal");

    const auto title_case = speller::BuildAutoWordSegmentationCandidate(
        L"Tuttat1", L"Tutt\u00E1t", InputMethod::VNI,
        CorrectionLevel::Experimental);
    assert_true(title_case.has_value(),
                "Segmentation accepts explicit title casing");
    if (title_case) {
        assert_eq(title_case->text, L"T\u00FAt t\u00E1t",
                  "Segmentation preserves title casing");
    }

    const auto upper_case = speller::BuildAutoWordSegmentationCandidate(
        L"TUTTAT1", L"TUTT\u00C1T", InputMethod::VNI,
        CorrectionLevel::Experimental);
    assert_true(upper_case.has_value(),
                "Experimental segmentation accepts uppercase input");
    if (upper_case) {
        assert_eq(upper_case->text, L"T\u00DAT T\u00C1T",
                  "Segmentation preserves all-uppercase casing");
    }

    assert_true(
        !speller::BuildAutoWordSegmentationCandidate(
             L"tuttat2", L"tutt\u00E0t", InputMethod::VNI,
             CorrectionLevel::Experimental) &&
        !speller::BuildAutoWordSegmentationCandidate(
             L"tuttat1", L"tutt\u00E1t", InputMethod::Telex,
             CorrectionLevel::Experimental),
        "Wrong explicit tone or input-method key rejects the candidate");

    assert_true(
        !speller::BuildAutoWordSegmentationCandidate(
             L"banhang", L"banhang", InputMethod::Telex,
             CorrectionLevel::Experimental) &&
        !speller::BuildAutoWordSegmentationCandidate(
             L"banhangf", L"banh\u00E0ng", InputMethod::Telex,
             CorrectionLevel::Experimental) &&
        !speller::BuildAutoWordSegmentationCandidate(
             L"banhang2", L"banh\u00E0ng", InputMethod::VNI,
             CorrectionLevel::Experimental),
        "Missing evidence and tied banhang candidates remain unchanged");
    assert_true(
        speller::CuratedWordSegmentationBigramCount() >= 600 &&
            speller::HasCuratedWordSegmentationPhrase(
            L"b\u1EA1n h\u00E0ng") &&
            speller::HasCuratedWordSegmentationPhrase(
                L"ph\u1EA7n m\u1EC1m") &&
            !speller::HasCuratedWordSegmentationPhrase(
                L"b\u1EA3n h\u00E0ng"),
        "Ambiguous phrase data contains bạn hàng, not bản hàng");

    assert_true(
        !build_from_engine(
             InputMethod::VNI, L"tuttat1", CorrectionLevel::Off) &&
        !build_from_engine(
             InputMethod::Telex, L"tuttats", CorrectionLevel::Normal) &&
        !build_from_engine(
             InputMethod::Telex, L"tuttats", CorrectionLevel::Advanced),
        "Only Experimental enables auto segmentation");

    const auto shaped_vni = speller::BuildAutoWordSegmentationCandidate(
        L"sanxuat61", L"sanxuat61", InputMethod::VNI,
        CorrectionLevel::Experimental);
    assert_true(shaped_vni.has_value(),
                "VNI vowel-shape evidence can select a curated phrase");
    if (shaped_vni) {
        assert_eq(shaped_vni->text, L"s\u1EA3n xu\u1EA5t",
                  "Segmentation preserves explicit VNI vowel shape and tone");
    }

    const std::wstring long_raw(
        speller::kMaxAutoWordSegmentationRawLength + 1, L'a');
    assert_true(
        !speller::BuildAutoWordSegmentationCandidate(
             long_raw, long_raw, InputMethod::Telex,
             CorrectionLevel::Experimental),
        "Auto segmentation rejects raw tokens longer than 24 characters");

    constexpr size_t kWarmupIterations = 100;
    constexpr size_t kLatencyIterations = 10000;
    size_t observed_candidates = 0;
    for (size_t iteration = 0; iteration < kWarmupIterations; ++iteration) {
        observed_candidates +=
            speller::BuildAutoWordSegmentationCandidate(
                L"tuttats", L"tutt\u00E1t", InputMethod::Telex,
                CorrectionLevel::Experimental).has_value();
    }
    const auto start = std::chrono::steady_clock::now();
    for (size_t iteration = 0; iteration < kLatencyIterations; ++iteration) {
        observed_candidates +=
            speller::BuildAutoWordSegmentationCandidate(
                L"tuttats", L"tutt\u00E1t", InputMethod::Telex,
                CorrectionLevel::Experimental).has_value();
    }
    const double latency_us = std::chrono::duration<double, std::micro>(
        std::chrono::steady_clock::now() - start).count() /
        static_cast<double>(kLatencyIterations);
    std::cout << "  Auto word segmentation candidate average: "
              << latency_us << " us/call" << std::endl;
    assert_true(
        observed_candidates == kWarmupIterations + kLatencyIterations,
        "Segmentation latency loop retains every candidate");
    assert_true(latency_us < 1000.0,
                "Segmentation candidate builder stays under 1 ms/call");
}

void test_auto_word_segmentation_commit_decision() {
    std::cout << "\nRunning test_auto_word_segmentation_commit_decision..."
              << std::endl;

    const auto decide = [](
        std::wstring_view raw, std::wstring_view display,
        InputMethod method,
        wchar_t delimiter = L' ',
        CorrectionLevel level = CorrectionLevel::Experimental,
        bool enabled = true,
        bool secure = false,
        bool shorthand = false) {
        return DecideCommitTransform({
            raw, display, method, level, delimiter,
            enabled, secure, shorthand,
        });
    };

    const auto vni = decide(L"tuttat1", L"tuttat1", InputMethod::VNI);
    assert_true(
        vni.transform_kind ==
                vn_ime::CommitUndoEntry::TransformKind::WordSegmentation,
        "Commit decision accepts VNI raw-literal runtime display");
    assert_eq(vni.text, L"t\u00FAt t\u00E1t",
              "VNI commit decision segments raw-literal token");

    const auto telex = decide(
        L"tuttats", L"tuttats", InputMethod::Telex);
    assert_true(
        telex.transform_kind ==
                vn_ime::CommitUndoEntry::TransformKind::WordSegmentation,
        "Commit decision accepts Telex raw-literal runtime display");
    assert_eq(telex.text, L"t\u00FAt t\u00E1t",
              "Telex commit decision segments raw-literal token");

    const auto per_syllable_vni = decide(
        L"hoang2hon6", L"hoang2hon6", InputMethod::VNI);
    assert_true(
        per_syllable_vni.transform_kind ==
                vn_ime::CommitUndoEntry::TransformKind::WordSegmentation,
        "Commit decision accepts canonical VNI keys for both syllables");
    assert_eq(per_syllable_vni.text, L"ho\u00E0ng h\u00F4n",
              "VNI commit decision segments hoang2hon6");

    const auto kiem_tra_vni = decide(
        L"kie63mtra", L"kie63mtra", InputMethod::VNI);
    assert_true(
        kiem_tra_vni.transform_kind ==
                vn_ime::CommitUndoEntry::TransformKind::WordSegmentation,
        "Commit decision recognizes VNI kiem tra bigram");
    assert_eq(kiem_tra_vni.text, L"ki\u1EC3m tra",
              "VNI commit decision segments kie63mtra");

    const auto english = decide(
        L"access", L"access", InputMethod::Telex);
    assert_true(
        english.transform_kind ==
                vn_ime::CommitUndoEntry::TransformKind::None &&
            english.text == L"access",
        "Exact common English token bypasses commit segmentation");

    for (const auto& blocked : {
             decide(L"tuttats", L"tuttats", InputMethod::Telex,
                    L' ', CorrectionLevel::Experimental, false),
             decide(L"tuttats", L"tuttats", InputMethod::Telex,
                    L' ', CorrectionLevel::Normal),
             decide(L"tuttats", L"tuttats", InputMethod::Telex,
                    L' ', CorrectionLevel::Advanced),
             decide(L"tuttats", L"tuttats", InputMethod::Telex, L'.'),
             decide(L"tuttats", L"tuttats", InputMethod::Telex,
                    L' ', CorrectionLevel::Experimental, true, true)}) {
        assert_true(
            blocked.transform_kind ==
                    vn_ime::CommitUndoEntry::TransformKind::None &&
                blocked.text == L"tuttats",
            "Option, level, delimiter, and secure gates preserve original text");
    }

    assert_true(
        IsNarrowSegmentationProtectedToken(
            L"toan@gmail.com") &&
            IsNarrowSegmentationProtectedToken(L"sha256") &&
            !IsNarrowSegmentationProtectedToken(L"hoang2hon6"),
        "Smart Context blocks email/code without blocking VNI phrase evidence");

    const auto shorthand = decide(
        L"vn", L"Vi\u1EC7t Nam", InputMethod::Telex,
        L' ', CorrectionLevel::Experimental, true, false, true);
    assert_true(
        shorthand.transform_kind ==
                vn_ime::CommitUndoEntry::TransformKind::ShorthandExpansion &&
            shorthand.text == L"Vi\u1EC7t Nam",
        "Exact shorthand takes precedence over segmentation");

    const auto title = decide(
        L"Tuttat1", L"Tuttat1", InputMethod::VNI);
    assert_eq(title.text, L"T\u00FAt t\u00E1t",
              "Commit decision preserves title casing");

    const auto direct_parity = decide(
        L"tuttats", L"tuttats", InputMethod::SimpleTelex);
    assert_true(
        direct_parity.transform_kind == telex.transform_kind &&
            direct_parity.text == telex.text,
        "Shared commit decision gives TSF/direct-inline parity");

    const auto web_space_plan = DecideHostOwnedSpaceCommit(
        true, true, false, true, false);
    assert_true(
        web_space_plan.target == HostOwnedSpaceCommitTarget::Composition &&
            web_space_plan.host_owned_commit_delimiter == L' ' &&
            web_space_plan.ime_insertion_character == L'\0' &&
            web_space_plan.pass_key_to_host,
        "Web native Space supplies transform boundary without IME insertion");
    assert_true(
        ResolveCommitTransformDelimiter(
            web_space_plan.ime_insertion_character,
            web_space_plan.host_owned_commit_delimiter) == L' ',
        "Host-owned web Space reaches the shared commit transform");

    const auto word_composition_space_plan = DecideHostOwnedSpaceCommit(
        true, false, true, true, false);
    const auto word_direct_space_plan = DecideHostOwnedSpaceCommit(
        true, false, true, false, true);
    assert_true(
        word_composition_space_plan.target ==
                HostOwnedSpaceCommitTarget::Composition &&
            word_direct_space_plan.target ==
                HostOwnedSpaceCommitTarget::DirectInline &&
            word_direct_space_plan.host_owned_commit_delimiter == L'\0' &&
            word_direct_space_plan.ime_insertion_character == L' ' &&
            !word_direct_space_plan.pass_key_to_host &&
            ResolveCommitTransformDelimiter(
                word_direct_space_plan.ime_insertion_character,
                word_direct_space_plan.host_owned_commit_delimiter) == L' ',
        "Word direct-inline Space stays inside the edit-session transaction");

    const auto native_punctuation_plan = DecideHostOwnedSpaceCommit(
        false, true, false, true, false);
    assert_true(
        native_punctuation_plan.target ==
                HostOwnedSpaceCommitTarget::None &&
            native_punctuation_plan.host_owned_commit_delimiter == L'\0' &&
            ResolveCommitTransformDelimiter(L'\0', L'\0') == L'\0',
        "Native punctuation has no segmentation boundary or IME insertion");

    const auto utf16_span = ComputeDirectCommitRewriteSpan(
        109, 9, std::wstring_view(L"s\u1EA3n xu\u1EA5t").length());
    assert_true(
        utf16_span.has_value() && utf16_span->start == 100 &&
            utf16_span->old_end == 109 &&
            utf16_span->new_caret == 108,
        "Changed-length direct rewrite updates UTF-16 caret before Space");

    const size_t segmented_utf8_length =
        to_utf8(L"t\u00FAt t\u00E1t").length();
    const auto utf8_span = ComputeDirectCommitRewriteSpan(
        207, 7, segmented_utf8_length);
    assert_true(
        utf8_span.has_value() && utf8_span->start == 200 &&
            utf8_span->old_end == 207 &&
            utf8_span->new_caret == 200 + segmented_utf8_length,
        "Changed-length Scintilla rewrite updates UTF-8 byte caret before Space");
}

std::wstring reference_strip_all_accents(std::wstring_view value) {
    std::wstring result;
    result.reserve(value.length());
    for (const wchar_t character : value) {
        rules::VowelData vowel{};
        if (rules::GetVowelData(character, vowel)) {
            result.push_back(vowel.raw);
        } else if (character == L'\u0111' || character == L'\u0110') {
            result.push_back(L'd');
        } else {
            result.push_back(rules::ToLower(character));
        }
    }
    return result;
}

size_t reference_damerau_levenshtein(
    std::wstring_view first,
    std::wstring_view second) {
    if (first.length() > 14 || second.length() > 14) {
        return 999;
    }
    int distance[16][16]{};
    for (size_t index = 0; index <= first.length(); ++index) {
        distance[index][0] = static_cast<int>(index);
    }
    for (size_t index = 0; index <= second.length(); ++index) {
        distance[0][index] = static_cast<int>(index);
    }
    for (size_t first_index = 1; first_index <= first.length();
         ++first_index) {
        for (size_t second_index = 1; second_index <= second.length();
             ++second_index) {
            const int cost = first[first_index - 1] == second[second_index - 1]
                ? 0 : 1;
            distance[first_index][second_index] = (std::min)({
                distance[first_index - 1][second_index] + 1,
                distance[first_index][second_index - 1] + 1,
                distance[first_index - 1][second_index - 1] + cost,
            });
            if (first_index > 1 && second_index > 1 &&
                first[first_index - 1] == second[second_index - 2] &&
                first[first_index - 2] == second[second_index - 1]) {
                distance[first_index][second_index] = (std::min)(
                    distance[first_index][second_index],
                    distance[first_index - 2][second_index - 2] + cost);
            }
        }
    }
    return static_cast<size_t>(distance[first.length()][second.length()]);
}

std::optional<std::wstring> reference_experimental_damerau_candidate(
    std::wstring_view input,
    size_t* minimum_match_count = nullptr) {
    const std::wstring flat_input = reference_strip_all_accents(input);
    if (flat_input.length() > 14) {
        if (minimum_match_count) {
            *minimum_match_count = 0;
        }
        return std::nullopt;
    }
    const size_t max_distance = flat_input.length() <= 5 ? 1 : 2;
    size_t minimum_distance = max_distance + 1;
    size_t match_count = 0;
    std::wstring_view best_match;
    for (const std::wstring_view dictionary_word : speller::DICTIONARY) {
        const std::wstring flat_dictionary =
            reference_strip_all_accents(dictionary_word);
        const size_t length_difference =
            flat_dictionary.length() > flat_input.length()
            ? flat_dictionary.length() - flat_input.length()
            : flat_input.length() - flat_dictionary.length();
        if (length_difference > max_distance) {
            continue;
        }
        const size_t distance = reference_damerau_levenshtein(
            flat_input, flat_dictionary);
        if (distance > max_distance) {
            continue;
        }
        if (distance < minimum_distance) {
            minimum_distance = distance;
            best_match = dictionary_word;
            match_count = 1;
        } else if (distance == minimum_distance) {
            ++match_count;
        }
    }
    if (minimum_match_count) {
        *minimum_match_count = match_count;
    }
    if (match_count != 1 || best_match.empty()) {
        return std::nullopt;
    }
    return std::wstring(best_match);
}

void test_damerau_levenshtein_experimental() {
    std::cout << "\nRunning test_damerau_levenshtein_experimental..." << std::endl;

    // 1. English word "is" protection: must not be corrected to "si"
    {
        speller::CorrectionResult res = speller::CorrectWordEx(L"is", L"is", CorrectionLevel::Advanced, InputMethod::Telex);
        assert_true(!res.changed, "is is not corrected to si under Advanced level");
        assert_eq(res.word, L"is", "Word stays is under Advanced level");
    }
    {
        speller::CorrectionResult res = speller::CorrectWordEx(L"is", L"is", CorrectionLevel::Experimental, InputMethod::Telex);
        assert_true(!res.changed, "is is not corrected to si under Experimental level");
        assert_eq(res.word, L"is", "Word stays is under Experimental level");
    }

    // 2. Experimental Damerau-Levenshtein typo correction (scrambled words)
    {
        speller::CorrectionResult res = speller::CorrectWordEx(L"đườgn", L"dduowgnf", CorrectionLevel::Experimental, InputMethod::Telex);
        assert_true(res.changed, "Experimental corrects dduowgnf/đườgn");
        assert_eq(res.word, L"đường", "đườgn corrected to đường");
    }
    {
        Engine engine(InputMethod::Telex);
        engine.SetCorrectionLevel(CorrectionLevel::Experimental);
        type_string(engine, L"dduowgnf");
        assert_eq(engine.GetDisplayString(), L"đường", "Engine typed dduowgnf -> đường under Experimental");
    }

    // 3. English words protection (struct, github, const)
    {
        speller::CorrectionResult res = speller::CorrectWordEx(L"struct", L"struct", CorrectionLevel::Experimental, InputMethod::Telex);
        assert_true(!res.changed, "struct is not corrected under Experimental");
    }
    {
        speller::CorrectionResult res = speller::CorrectWordEx(L"github", L"github", CorrectionLevel::Experimental, InputMethod::Telex);
        assert_true(!res.changed, "github is not corrected under Experimental");
    }
    {
        speller::CorrectionResult res = speller::CorrectWordEx(L"const", L"const", CorrectionLevel::Experimental, InputMethod::Telex);
        assert_true(!res.changed, "const is not corrected under Experimental");
    }

    // 4. Casing preservation
    {
        speller::CorrectionResult res = speller::CorrectWordEx(L"Đườgn", L"Dduowgnf", CorrectionLevel::Experimental, InputMethod::Telex);
        assert_true(res.changed, "Đườgn is corrected under Experimental");
        assert_eq(res.word, L"Đường", "Đườgn corrected to Đường with casing preserved");
    }

    // Direct Experimental Damerau path: score 850 distinguishes it from the
    // earlier Advanced swap rules, which use score 900.
    for (const auto& [input, expected] :
         std::array<std::pair<std::wstring_view, std::wstring_view>, 2>{
             std::pair{L"b\u00F3ogn", L"boong"},
             std::pair{L"B\u00F3ogn", L"Boong"},
         }) {
        const speller::CorrectionResult result = speller::CorrectWordEx(
            input, input, CorrectionLevel::Experimental,
            InputMethod::Telex, EnglishProtectionLevel::Off);
        assert_true(result.changed && result.score == 850 &&
                        result.kind == speller::CorrectionKind::AdjacentKeySwap,
                    "Experimental direct Damerau transposition keeps kind and score");
        assert_eq(result.word, std::wstring(expected),
                  "Experimental direct Damerau preserves casing");
    }

    for (const CorrectionLevel level : {
             CorrectionLevel::Normal, CorrectionLevel::Advanced}) {
        const speller::CorrectionResult result = speller::CorrectWordEx(
            L"b\u00F3ogn", L"b\u00F3ogn", level,
            InputMethod::Telex, EnglishProtectionLevel::Off);
        assert_eq(result.word, L"b\u00F3ogn",
                  "Normal and Advanced do not enable general Damerau");
    }

    const speller::CorrectionResult miss = speller::CorrectWordEx(
        L"zzzzzf", L"zzzzzf", CorrectionLevel::Experimental,
        InputMethod::Telex, EnglishProtectionLevel::Off);
    assert_true(!miss.changed && miss.score == 0,
                "Experimental Damerau miss stays unchanged");

    size_t ambiguous_match_count = 0;
    const auto ambiguous_reference =
        reference_experimental_damerau_candidate(
            L"\u0129a", &ambiguous_match_count);
    const speller::CorrectionResult ambiguous = speller::CorrectWordEx(
        L"\u0129a", L"\u0129a", CorrectionLevel::Experimental,
        InputMethod::Telex, EnglishProtectionLevel::Off);
    assert_true(!speller::IsInDictionary(L"\u0129a") &&
                    !ambiguous_reference && ambiguous_match_count > 1 &&
                    !ambiguous.changed && ambiguous.score == 0,
                "Experimental ambiguous minimum stays unchanged");

    const speller::CorrectionResult short_distance_two =
        speller::CorrectWordEx(
            L"b\u00F3ogx", L"b\u00F3ogx",
            CorrectionLevel::Experimental, InputMethod::Telex,
            EnglishProtectionLevel::Off);
    assert_true(std::wstring_view(L"b\u00F3ogx").length() == 5 &&
                    reference_damerau_levenshtein(
                        reference_strip_all_accents(L"b\u00F3ogx"),
                        L"boong") == 2 &&
                    !reference_experimental_damerau_candidate(L"b\u00F3ogx") &&
                    !short_distance_two.changed,
                "Five-character input rejects distance two");

    const speller::CorrectionResult long_distance_two =
        speller::CorrectWordEx(
            L"cq\u00FA\u00EAcx", L"cq\u00FA\u00EAcx",
            CorrectionLevel::Experimental, InputMethod::Telex,
            EnglishProtectionLevel::Off);
    assert_true(std::wstring_view(L"cq\u00FA\u00EAcx").length() == 6 &&
                    reference_damerau_levenshtein(
                        reference_strip_all_accents(L"cq\u00FA\u00EAcx"),
                        L"chu\u00EAch") == 2 &&
                    long_distance_two.changed &&
                    long_distance_two.score == 850,
                "Six-character input accepts unique distance two");
    assert_eq(long_distance_two.word, L"chu\u1EC7ch",
              "Distance-two boundary candidate remains stable");

    for (const std::wstring_view input : {
             L"b\u00F3ogn", L"B\u00F3ogn", L"zzzzzf", L"\u0129a",
             L"b\u00F3ogx", L"cq\u00FA\u00EAcx"}) {
        const auto reference = reference_experimental_damerau_candidate(input);
        const speller::CorrectionResult optimized = speller::CorrectWordEx(
            input, input, CorrectionLevel::Experimental,
            InputMethod::Telex, EnglishProtectionLevel::Off);
        const std::wstring expected = reference
            ? speller::PreserveCasing(input, *reference)
            : std::wstring(input);
        assert_eq(optimized.word, expected,
                  "Optimized Damerau matches full-matrix reference corpus");
    }

    constexpr size_t latency_iterations = 1200;
    size_t observed_hits = 0;
    for (size_t iteration = 0; iteration < 100; ++iteration) {
        observed_hits += speller::CorrectWordEx(
            L"b\u00F3ogn", L"b\u00F3ogn",
            CorrectionLevel::Experimental, InputMethod::Telex,
            EnglishProtectionLevel::Off).changed;
    }
    const auto latency_start = std::chrono::steady_clock::now();
    for (size_t iteration = 0; iteration < latency_iterations; ++iteration) {
        observed_hits += speller::CorrectWordEx(
            L"b\u00F3ogn", L"b\u00F3ogn",
            CorrectionLevel::Experimental, InputMethod::Telex,
            EnglishProtectionLevel::Off).changed;
    }
    const double latency_us = std::chrono::duration<double, std::micro>(
        std::chrono::steady_clock::now() - latency_start).count() /
        static_cast<double>(latency_iterations);
    std::cout << "  Experimental Damerau optimized average: "
              << latency_us << " us/call" << std::endl;
    assert_true(observed_hits == latency_iterations + 100,
                "Damerau latency loop retains every unique hit");
    assert_true(latency_us < 1500.0,
                "Damerau optimized path stays under broad latency guard");

    // 5. Adjacent Initial Key Swap (Advanced level and above)
    {
        speller::CorrectionResult resN = speller::CorrectWordEx(L"gnon", L"gnon", CorrectionLevel::Normal, InputMethod::Telex);
        assert_true(!resN.changed, "gnon unchanged under Normal");

        speller::CorrectionResult resA = speller::CorrectWordEx(L"gnon", L"gnon", CorrectionLevel::Advanced, InputMethod::Telex);
        assert_true(resA.changed, "Advanced corrects gnon -> ngon");
        assert_eq(resA.word, L"ngon", "gnon corrected to ngon");
    }
    {
        speller::CorrectionResult resA = speller::CorrectWordEx(L"hcao", L"hcao", CorrectionLevel::Advanced, InputMethod::Telex);
        assert_true(resA.changed, "Advanced corrects hcao -> chao");
        assert_eq(resA.word, L"chao", "hcao corrected to chao");
    }
    {
        speller::CorrectionResult resA = speller::CorrectWordEx(L"hpong", L"hpong", CorrectionLevel::Advanced, InputMethod::Telex);
        assert_true(resA.changed, "Advanced corrects hpong -> phong");
        assert_eq(resA.word, L"phong", "hpong corrected to phong");
    }
    {
        speller::CorrectionResult resA = speller::CorrectWordEx(L"hnay", L"hnay", CorrectionLevel::Advanced, InputMethod::Telex);
        assert_true(resA.changed, "Advanced corrects hnay -> nhay");
        assert_eq(resA.word, L"nhay", "hnay corrected to nhay");
    }
}

void test_english_word_protection() {
    std::cout << "\nRunning test_english_word_protection..." << std::endl;
    const wchar_t* words[] = {
        L"us", L"is", L"in", L"on", L"at", L"by", L"to", L"if", L"me", L"we",
        L"do", L"go", L"no", L"so", L"up", L"app", L"api", L"git", L"dev", L"sql", L"code"
    };

    for (const wchar_t* w : words) {
        speller::CorrectionResult resProt = speller::CorrectWordEx(w, w, CorrectionLevel::Experimental, InputMethod::Telex, true);
        assert_true(!resProt.changed, "English word is protected when protection is enabled");
        assert_eq(resProt.word, w, "Word remains unchanged");
    }

    Engine engine;
    engine.SetInputMethod(InputMethod::VNI);
    engine.SetCorrectionLevel(CorrectionLevel::Experimental);
    engine.SetEnglishProtection(true);

    speller::CorrectionResult resProt = speller::CorrectWordEx(L"us", L"us", CorrectionLevel::Experimental, InputMethod::VNI, true);
    assert_eq(resProt.word, L"us", "speller output for 'us' with English protection enabled");

    speller::CorrectionResult resNoProt = speller::CorrectWordEx(L"us", L"us", CorrectionLevel::Experimental, InputMethod::VNI, false);
    assert_eq(resNoProt.word, L"su", "speller output for 'us' with English protection disabled");

    assert_true(speller::CommonEnglishWordsAreSorted(),
                "Common English constexpr data remains sorted");
    assert_true(
        speller::BilingualEnglishWordCount() == 5118 &&
            speller::BilingualEnglishCommonWordCount() == 3985 &&
            speller::BilingualEnglishExtendedWordCount() == 1133,
        "Bilingual English lexicon exposes stable tier counts");
    assert_true(
        speller::LookupBilingualEnglishWord(L"Addressed") ==
                speller::EnglishLexiconTier::Common &&
            speller::LookupBilingualEnglishWord(L"researcher") ==
                speller::EnglishLexiconTier::Extended &&
            speller::LookupBilingualEnglishWord(L"alo") ==
                speller::EnglishLexiconTier::None &&
            speller::LookupBilingualEnglishWord(L"notawordzz") ==
                speller::EnglishLexiconTier::None &&
            speller::LookupBilingualEnglishWord(L"tiếng") ==
                speller::EnglishLexiconTier::None,
        "Packed bilingual lookup handles common, extended, mixed-case and non-ASCII words");

    size_t generated_common = 0;
    size_t generated_extended = 0;
    size_t generated_preserved = 0;
    bool generated_lookup_valid = true;
    for (size_t index = 0;
         index < speller::data::kEnglishLexiconWordCount; ++index) {
        const char* ascii = speller::data::kEnglishLexiconBlob +
            speller::data::kEnglishLexiconOffsets[index];
        const std::string_view ascii_word(ascii);
        const std::wstring word(ascii_word.begin(), ascii_word.end());
        const auto expected_tier = static_cast<speller::EnglishLexiconTier>(
            speller::data::kEnglishLexiconTiers[index]);
        generated_common +=
            expected_tier == speller::EnglishLexiconTier::Common;
        generated_extended +=
            expected_tier == speller::EnglishLexiconTier::Extended;
        generated_lookup_valid = generated_lookup_valid &&
            speller::LookupBilingualEnglishWord(word) == expected_tier;
        for (const InputMethod method : {
                 InputMethod::Telex,
                 InputMethod::SimpleTelex,
                 InputMethod::VNI}) {
            generated_preserved +=
                speller::ClassifyEnglishProtection(
                    word, L"", method,
                    EnglishProtectionLevel::EnglishFirst) ==
                speller::EnglishProtectionDecision::PreserveRaw;
        }
    }
    assert_true(
        generated_lookup_valid &&
            generated_common == speller::BilingualEnglishCommonWordCount() &&
            generated_extended == speller::BilingualEnglishExtendedWordCount() &&
            generated_preserved == speller::BilingualEnglishWordCount() * 3,
        "Every generated word round-trips and English First protects all methods");
    assert_true(speller::IsCommonEnglishWord(L"exe"), "Sorted English lookup finds exe");
    assert_true(speller::IsCommonEnglishWord(L"exec"), "Sorted English lookup finds exec");
    assert_true(speller::IsCommonEnglishWord(L"res"), "Sorted English lookup finds res");
    assert_true(speller::IsCommonEnglishWord(L"reset"), "Sorted English lookup finds reset");
    assert_true(
        speller::StrongEnglishProtectionWords().size() == 89 &&
            speller::IsStrongEnglishProtectionWord(L"DNA") &&
            speller::IsStrongEnglishProtectionWord(L"rna") &&
            speller::IsStrongEnglishProtectionWord(L"mit") &&
            speller::IsStrongEnglishProtectionWord(L"GNU") &&
            speller::IsStrongEnglishProtectionWord(L"VNI") &&
            speller::IsStrongEnglishProtectionWord(L"macOS") &&
            speller::IsCommonEnglishWord(L"status") &&
            speller::IsCommonEnglishWord(L"ssh"),
        "Strong English lookup covers conflict words and technical acronyms");

    auto typed = [](InputMethod method, CorrectionLevel correction,
                    EnglishProtectionLevel protection, std::wstring_view keys,
                    bool smart_context_protection = true) {
        Engine e(method);
        e.SetCorrectionLevel(correction);
        e.SetEnglishProtectionLevel(protection);
        e.SetSmartContextProtection(smart_context_protection);
        type_string(e, keys);
        return e.GetDisplayString();
    };

    for (const InputMethod method : {
             InputMethod::Telex,
             InputMethod::SimpleTelex,
             InputMethod::VNI}) {
        assert_eq(
            typed(method, CorrectionLevel::Experimental,
                  EnglishProtectionLevel::Balanced, L"addressed", false),
            L"addressed",
            "Balanced protects a generated Common English word");
        assert_eq(
            typed(method, CorrectionLevel::Experimental,
                  EnglishProtectionLevel::EnglishFirst, L"researcher", false),
            L"researcher",
            "English First protects a generated Extended English word");
    }
    const std::wstring researcher_without_bilingual = typed(
        InputMethod::Telex, CorrectionLevel::Experimental,
        EnglishProtectionLevel::Off, L"researcher", false);
    assert_true(
        researcher_without_bilingual != L"researcher" &&
            typed(InputMethod::Telex, CorrectionLevel::Experimental,
                  EnglishProtectionLevel::Balanced, L"researcher", false) ==
                researcher_without_bilingual,
        "Balanced does not consume the Extended-only English tier");

    bool all_strong_words_preserved = true;
    for (const std::wstring_view word :
         speller::StrongEnglishProtectionWords()) {
        if (!speller::IsCommonEnglishWord(word) ||
            speller::ClassifyEnglishProtection(
                word, L"", InputMethod::Telex,
                EnglishProtectionLevel::Balanced) !=
                speller::EnglishProtectionDecision::PreserveRaw) {
            all_strong_words_preserved = false;
            break;
        }
        for (const InputMethod method : {
                 InputMethod::Telex,
                 InputMethod::SimpleTelex,
                 InputMethod::VNI}) {
            if (typed(
                    method, CorrectionLevel::Experimental,
                    EnglishProtectionLevel::Balanced, word, false) != word) {
                all_strong_words_preserved = false;
                break;
            }
        }
        if (!all_strong_words_preserved) {
            break;
        }
    }
    assert_true(
        all_strong_words_preserved,
        "Balanced protection preserves all 89 strong English words in every input method");
    assert_eq(
        typed(InputMethod::Telex, CorrectionLevel::Experimental,
              EnglishProtectionLevel::Balanced, L"macOS", false),
        L"macOS", "Strong English lookup preserves mixed casing");
    assert_eq(
        typed(InputMethod::Telex, CorrectionLevel::Normal,
              EnglishProtectionLevel::Off, L"too", false),
        L"t\u00F4", "Turning English protection Off restores native Telex rules");

    assert_true(
        speller::HasProtectedEnglishBigramSplit(L"statusbar") &&
            speller::HasProtectedEnglishBigramSplit(L"vnimode") &&
            speller::HasProtectedEnglishBigramSplit(L"mitlinux") &&
            !speller::HasProtectedEnglishBigramSplit(L"researcherbarrister") &&
            !speller::HasProtectedEnglishBigramSplit(L"antam") &&
            !speller::HasProtectedEnglishBigramSplit(L"trangweb"),
        "Bigram guard protects two English words without blocking mixed Vietnamese phrases");

    for (const InputMethod method : {InputMethod::Telex, InputMethod::SimpleTelex}) {
        for (const CorrectionLevel correction : {
                 CorrectionLevel::Normal,
                 CorrectionLevel::Advanced,
                 CorrectionLevel::Experimental}) {
            for (const std::wstring_view word : {
                     L"access", L"class", L"password", L"reset",
                     L"user", L"text", L"exe", L"res", L"book"}) {
                assert_eq(typed(method, correction, EnglishProtectionLevel::Balanced, word),
                          std::wstring(word),
                          "Balanced Engine path preserves certain English/code word");
            }
        }
    }

    for (const InputMethod method : {InputMethod::Telex, InputMethod::SimpleTelex}) {
        for (const CorrectionLevel correction : {
                 CorrectionLevel::Advanced, CorrectionLevel::Experimental}) {
            assert_eq(typed(method, correction, EnglishProtectionLevel::Balanced, L"book"),
                      L"book", "Balanced Engine path protects book at high correction levels");
        }
    }

    struct EnglishVietnameseCollision {
        std::wstring_view raw;
        std::wstring_view vietnamese;
    };
    static constexpr EnglishVietnameseCollision collisions[] = {
        {L"as", L"\u00E1"}, {L"is", L"\u00ED"}, {L"us", L"\u00FA"},
        {L"if", L"\u00EC"}, {L"of", L"\u00F2"}, {L"or", L"\u1ECF"},
        {L"bar", L"b\u1EA3"}, {L"best", L"b\u00E9t"}, {L"bus", L"b\u00FA"},
        {L"car", L"c\u1EA3"}, {L"host", L"h\u00F3t"}, {L"last", L"l\u00E1t"},
        {L"list", L"l\u00EDt"}, {L"max", L"m\u00E3"}, {L"test", L"t\u00E9t"},
        {L"this", L"th\u00ED"}, {L"var", L"v\u1EA3"},
    };
    for (const auto& collision : collisions) {
        assert_true(speller::IsInDictionary(collision.vietnamese),
                    "Balanced collision output exists in Vietnamese dictionary");
        for (const InputMethod method : {InputMethod::Telex, InputMethod::SimpleTelex}) {
            assert_eq(typed(method, CorrectionLevel::Normal,
                            EnglishProtectionLevel::Balanced, collision.raw),
                      std::wstring(collision.vietnamese),
                      "Balanced keeps canonical Vietnamese collision through Engine");
        }
    }

    for (const InputMethod method : {InputMethod::Telex, InputMethod::SimpleTelex}) {
        for (const std::wstring_view word : {L"as", L"is", L"test", L"var"}) {
            assert_eq(typed(method, CorrectionLevel::Experimental,
                            EnglishProtectionLevel::EnglishFirst, word),
                      std::wstring(word),
                      "English First wins an ambiguous Telex collision");
        }
        assert_eq(typed(method, CorrectionLevel::Normal,
                        EnglishProtectionLevel::Off, L"as"),
                  L"\u00E1", "English protection Off keeps pure Telex conversion");
        assert_eq(typed(method, CorrectionLevel::Normal,
                        EnglishProtectionLevel::Off, L"reset"),
                  L"r\u1EBFt", "Off exposes noncanonical English token conversion");
    }

    static constexpr std::wstring_view top_100_english_smoke[] = {
        L"the", L"be", L"to", L"of", L"and", L"a", L"in", L"that", L"have", L"it",
        L"for", L"not", L"on", L"with", L"he", L"as", L"you", L"do", L"at", L"this",
        L"but", L"his", L"by", L"from", L"they", L"we", L"say", L"her", L"she", L"or",
        L"an", L"will", L"my", L"one", L"all", L"would", L"there", L"their", L"what", L"so",
        L"up", L"out", L"if", L"about", L"who", L"get", L"which", L"go", L"me", L"when",
        L"make", L"can", L"like", L"time", L"no", L"just", L"him", L"know", L"take", L"people",
        L"into", L"year", L"your", L"good", L"some", L"could", L"them", L"see", L"other", L"than",
        L"then", L"now", L"look", L"only", L"come", L"its", L"over", L"think", L"also", L"back",
        L"after", L"use", L"two", L"how", L"our", L"work", L"first", L"well", L"way", L"even",
        L"new", L"want", L"because", L"these", L"give", L"day", L"most", L"us",
    };
    for (const InputMethod method : {InputMethod::Telex, InputMethod::SimpleTelex}) {
        for (const CorrectionLevel correction : {
                 CorrectionLevel::Normal, CorrectionLevel::Experimental}) {
            for (const std::wstring_view word : top_100_english_smoke) {
                assert_eq(typed(method, correction,
                                EnglishProtectionLevel::EnglishFirst, word),
                          std::wstring(word),
                          "English First preserves curated top-100 word through Engine");
            }
        }
    }

    struct NewBalancedCollision {
        std::wstring_view raw;
        std::wstring_view expected;
        speller::EnglishProtectionDecision decision;
    };
    static constexpr NewBalancedCollision new_balanced_collisions[] = {
        {L"his", L"h\u00ED", speller::EnglishProtectionDecision::AmbiguousVietnamese},
        {L"her", L"her", speller::EnglishProtectionDecision::PreserveRaw},
        {L"she", L"she", speller::EnglishProtectionDecision::PreserveRaw},
        {L"there", L"there", speller::EnglishProtectionDecision::PreserveRaw},
        {L"who", L"who", speller::EnglishProtectionDecision::PreserveRaw},
        {L"now", L"n\u01A1", speller::EnglishProtectionDecision::AmbiguousVietnamese},
        {L"its", L"\u00EDt", speller::EnglishProtectionDecision::AmbiguousVietnamese},
        {L"two", L"two", speller::EnglishProtectionDecision::PreserveRaw},
        {L"how", L"h\u01A1", speller::EnglishProtectionDecision::AmbiguousVietnamese},
        {L"these", L"these", speller::EnglishProtectionDecision::PreserveRaw},
        {L"most", L"m\u00F3t", speller::EnglishProtectionDecision::AmbiguousVietnamese},
    };
    for (const auto& collision : new_balanced_collisions) {
        for (const InputMethod method : {InputMethod::Telex, InputMethod::SimpleTelex}) {
            const std::wstring processed = typed(
                method, CorrectionLevel::Normal, EnglishProtectionLevel::Off, collision.raw);
            assert_true(speller::ClassifyEnglishProtection(
                            collision.raw, processed, method,
                            EnglishProtectionLevel::Balanced) == collision.decision,
                        "Balanced classifies new common-English collision by canonical Vietnamese output");
            assert_eq(typed(method, CorrectionLevel::Normal,
                            EnglishProtectionLevel::Balanced, collision.raw),
                      std::wstring(collision.expected),
                      "Balanced applies intentional policy for new common-English collision");
        }
    }
    assert_eq(typed(InputMethod::Telex, CorrectionLevel::Experimental,
                    EnglishProtectionLevel::Balanced, L"Access"),
              L"Access", "Balanced raw restore preserves English casing");

    bool all_vni_words_preserved = true;
    for (const std::wstring_view word : speller::CommonEnglishWords()) {
        if (typed(InputMethod::VNI, CorrectionLevel::Experimental,
                  EnglishProtectionLevel::Balanced, word) != word) {
            all_vni_words_preserved = false;
            break;
        }
    }
    assert_true(all_vni_words_preserved,
                "VNI Balanced preserves every letter-only common English word through Engine");

    for (const std::wstring_view code : {L"win11", L"windows11", L"sha256", L"utf8"}) {
        assert_eq(typed(InputMethod::VNI, CorrectionLevel::Experimental,
                        EnglishProtectionLevel::Balanced, code),
                  std::wstring(code), "VNI Balanced preserves multi-character code token");
    }
    assert_eq(typed(InputMethod::VNI, CorrectionLevel::Experimental,
                    EnglishProtectionLevel::Off, L"windows11", false),
              L"windows1",
              "Disabling both protections restores native VNI digit processing");
    assert_eq(typed(InputMethod::VNI, CorrectionLevel::Normal,
                    EnglishProtectionLevel::Balanced, L"a1"),
              L"\u00E1", "VNI Balanced keeps a1 canonical");
    assert_eq(typed(InputMethod::VNI, CorrectionLevel::Normal,
                    EnglishProtectionLevel::Balanced, L"e6"),
              L"\u00EA", "VNI Balanced keeps e6 canonical");
    assert_eq(typed(InputMethod::VNI, CorrectionLevel::Normal,
                    EnglishProtectionLevel::Balanced, L"o6"),
              L"\u00F4", "VNI Balanced keeps o6 canonical");
    assert_eq(typed(InputMethod::VNI, CorrectionLevel::Normal,
                    EnglishProtectionLevel::Balanced, L"u7"),
              L"\u01B0", "VNI Balanced keeps u7 canonical");
    assert_eq(typed(InputMethod::VNI, CorrectionLevel::Normal,
                    EnglishProtectionLevel::Balanced, L"a8"),
              L"\u0103", "VNI Balanced keeps a8 canonical");
    assert_eq(typed(InputMethod::VNI, CorrectionLevel::Experimental,
                    EnglishProtectionLevel::EnglishFirst, L"a1"),
              L"\u00E1", "VNI English First does not disable canonical digit rules");

    assert_eq(type_text_committing_on_spaces(InputMethod::Telex, L"access ddas"),
              L"access \u0111\u00E1", "Mixed English/Vietnamese sentence commits correctly");

    constexpr size_t iterations = 100000;
    size_t preserved = 0;
    const auto start = std::chrono::steady_clock::now();
    for (size_t i = 0; i < iterations; ++i) {
        preserved += speller::ClassifyEnglishProtection(
            L"researcher", L"r\u1EBFearcher", InputMethod::Telex,
            EnglishProtectionLevel::EnglishFirst) ==
            speller::EnglishProtectionDecision::PreserveRaw;
    }
    const auto elapsed = std::chrono::duration<double, std::micro>(
        std::chrono::steady_clock::now() - start).count();
    const double average_us = elapsed / static_cast<double>(iterations);
    std::cout << "  Bilingual English classifier average: " << average_us << " us/call" << std::endl;
    assert_true(preserved == iterations, "Classifier latency loop executes all decisions");
    assert_true(average_us < 5.0, "Bilingual English classifier stays under latency guard");

    size_t protected_bigrams = 0;
    const auto bigram_start = std::chrono::steady_clock::now();
    for (size_t i = 0; i < iterations; ++i) {
        protected_bigrams += speller::HasProtectedEnglishBigramSplit(
            (i & 1) == 0 ? L"statusbar" : L"vnimode");
    }
    const double bigram_average_us =
        std::chrono::duration<double, std::micro>(
            std::chrono::steady_clock::now() - bigram_start).count() /
        static_cast<double>(iterations);
    std::cout << "  English bigram guard average: "
              << bigram_average_us << " us/call" << std::endl;
    assert_true(
        protected_bigrams == iterations,
        "English bigram latency loop detects every protected split");
    assert_true(
        bigram_average_us < 5.0,
        "English bigram guard stays under broad latency threshold");
}

void test_password_context_policy() {
    std::cout << "\n[Testing Password Context Policy]" << std::endl;

    using vn_ime::password_context::IsSecureInputContext;
    using vn_ime::password_context::SecureInputDecisionInput;
    using vn_ime::password_context::SupportsPasswordCharacterMessage;

    assert_true(SupportsPasswordCharacterMessage(L"Edit"),
                "Win32 Edit supports the password-character message");
    assert_true(SupportsPasswordCharacterMessage(L"richedit20a"),
                "ANSI RichEdit20A is recognized case-insensitively");
    assert_true(SupportsPasswordCharacterMessage(L"RICHEDIT50W"),
                "RichEdit50W supports the password-character message");
    assert_true(!SupportsPasswordCharacterMessage(L"WinDocumentView"),
                "CorelDRAW canvas is not queried with EM_GETPASSWORDCHAR");

    SecureInputDecisionInput input{};
    assert_true(IsSecureInputContext(input),
                "Missing focus HWND fails closed");

    input.has_window = true;
    assert_true(IsSecureInputContext(input),
                "Missing window class fails closed");

    input.class_name_available = true;
    assert_true(!IsSecureInputContext(input),
                "Known non-edit canvas remains an ordinary input context");

    input.password_message_control = true;
    assert_true(IsSecureInputContext(input),
                "Failed password-character query fails closed");

    input.password_query_succeeded = true;
    assert_true(!IsSecureInputContext(input),
                "Ordinary Edit with no password character is not secure input");

    input.password_character = L'*';
    assert_true(IsSecureInputContext(input),
                "Password character marks Edit as secure input");

    input.password_character = 0;
    input.password_style = true;
    assert_true(IsSecureInputContext(input),
                "ES_PASSWORD marks Edit as secure input");

    input = {};
    input.secure_desktop = true;
    assert_true(IsSecureInputContext(input),
                "Secure Desktop always disables text processing");

    input = {};
    input.password_input_scope = true;
    assert_true(IsSecureInputContext(input),
                "TSF password InputScope always disables text processing");
}

void test_fake_backspace_and_coreldraw_compatibility() {
    std::cout << "\n[Testing Fake Backspace & CorelDRAW Compatibility]" << std::endl;

    // CorelDRAW process detection
    assert_true(vn_ime::fake_backspace::IsCorelDrawProcess(L"coreldrw.exe"), "Detects coreldrw.exe");
    assert_true(vn_ime::fake_backspace::IsCorelDrawProcess(L"CorelDRW.exe"), "Detects CorelDRW.exe (case insensitive)");
    assert_true(vn_ime::fake_backspace::IsCorelDrawProcess(L"coreldraw.exe"), "Detects alternate coreldraw.exe name");
    assert_true(vn_ime::fake_backspace::IsCorelDrawProcess(L"C:\\Program Files\\Corel\\CorelDRAW Graphics Suite 2024\\Programs64\\CorelDRW.exe"), "Detects CorelDRW path");
    assert_true(!vn_ime::fake_backspace::IsCorelDrawProcess(L"corelpp.exe"), "Does not widen CorelDRAW routing to PHOTO-PAINT");
    assert_true(!vn_ime::fake_backspace::IsCorelDrawProcess(L"fontmanager.exe"), "Does not widen CorelDRAW routing to Font Manager");
    assert_true(!vn_ime::fake_backspace::IsCorelDrawProcess(L"notepad.exe"), "Does not match notepad.exe as Corel");
    assert_true(!vn_ime::fake_backspace::IsCorelDrawProcess(L"chrome.exe"), "Does not match chrome.exe as Corel");

    // Terminal / Console app detection
    assert_true(vn_ime::fake_backspace::IsTerminalProcess(L"windowsterminal.exe"), "Detects windowsterminal.exe");
    assert_true(vn_ime::fake_backspace::IsTerminalProcess(L"pwsh.exe"), "Detects pwsh.exe");
    assert_true(vn_ime::fake_backspace::IsTerminalProcess(L"cmd.exe"), "Detects cmd.exe");
    assert_true(vn_ime::fake_backspace::IsTerminalProcess(L"anydesk.exe"), "Detects anydesk.exe");

    // General Fake Backspace target detection
    assert_true(vn_ime::fake_backspace::IsFakeBackspaceTargetApp(L"coreldrw.exe", L""), "CorelDRAW host uses fake backspace");
    assert_true(vn_ime::fake_backspace::IsFakeBackspaceTargetApp(L"", L"CorelDRW.exe"), "Focused CorelDRAW process uses fake backspace");
    assert_true(vn_ime::fake_backspace::IsFakeBackspaceTargetApp(L"windowsterminal.exe", L""), "Target app for terminal");
    assert_true(vn_ime::fake_backspace::IsFakeBackspaceTargetApp(L"devenv.exe", L""), "Target app for Visual Studio");
    assert_true(!vn_ime::fake_backspace::IsFakeBackspaceTargetApp(L"notepad.exe", L"notepad.exe"), "Notepad is not fake backspace target");

    // ProcessFakeBackspaceChar inline buffer tracking
    Engine engine;
    engine.SetInputMethod(InputMethod::Telex);
    size_t inline_len = 0;

    const auto no_host_input =
        vn_ime::fake_backspace::HostInputDispatch::SuppressForTesting;

    // Type 'h', 'o', 'c', 'j'
    bool r1 = vn_ime::fake_backspace::ProcessFakeBackspaceChar(
        engine, L'h', inline_len, nullptr, false, no_host_input);
    assert_true(r1 && inline_len == 1, "Fake backspace processes 'h'");
    bool r2 = vn_ime::fake_backspace::ProcessFakeBackspaceChar(
        engine, L'o', inline_len, nullptr, false, no_host_input);
    assert_true(r2 && inline_len == 2, "Fake backspace processes 'ho'");
    bool r3 = vn_ime::fake_backspace::ProcessFakeBackspaceChar(
        engine, L'c', inline_len, nullptr, false, no_host_input);
    assert_true(r3 && inline_len == 3, "Fake backspace processes 'hoc'");
    bool r4 = vn_ime::fake_backspace::ProcessFakeBackspaceChar(
        engine, L'j', inline_len, nullptr, false, no_host_input);
    assert_true(r4 && inline_len == 3, "Fake backspace processes tone 'j' -> 'học'");
    assert_eq(engine.GetDisplayString(), L"học", "Engine display matches 'học'");

    // Test backspace
    bool rb = vn_ime::fake_backspace::ProcessFakeBackspaceBackspace(
        engine, inline_len, nullptr, false, no_host_input);
    assert_true(rb, "Fake backspace processes backspace");
    assert_eq(engine.GetDisplayString(), L"họ", "Engine display after backspace matches 'họ'");
    assert_true(inline_len == 2, "Inline length matches 'họ' length (2)");
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    std::cout << "========================================" << std::endl;
    std::cout << "   RUNNING CORE VIETNAMESE ENGINE TESTS " << std::endl;
    std::cout << "========================================" << std::endl;

    test_redundant_horn_key_dropping_for_uy();
    test_stale_modifier_override_correction();
    test_realtime_modifier_tone_before_vowel();
    test_browser_url_native_reconversion_policy();
    test_key_translation_without_state_mutation();
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
    test_app_input_profile_helpers();
    test_per_app_runtime_and_tray_policy();
    test_hotkey_toggle_state();
    test_shorthand_config_helpers();
    test_dynamic_shorthand_templates();
    test_shorthand_reload_policy();
    test_correction_level_config_mapping();
    test_smart_context_protection();
    test_engine_secure_clear();
    test_word_direct_inline_casing_sync();
    test_word_direct_inline_edit_session_recovery();
    test_composition_length_guard();
    test_composition_overflow_backspace_recovery();
    test_reconversion_length_guard();
    test_stress_and_latency();
    test_reconversion_span_latency();
    test_long_token_guard_latency();
    test_long_reconversion_candidate_latency();
    test_esc_restore_capture_predicate();
    test_commit_undo_backspace_restore_gate_and_boundary_spans();
    test_secure_clear_commit_undo_entry();
    test_commit_transform_caret_policy();
    test_dialog_vertical_fit_policy();
    test_smart_undo_metadata_gate_and_transaction();
    test_direct_inline_restore_span_verification();
    test_engine_correction_level_runtime();
    test_vietnamese_syllable_validity();
    test_speller_ex_candidates();
    test_advanced_correction_candidates();
    test_advanced_negative_cases();
    test_auto_word_segmentation_candidates();
    test_auto_word_segmentation_commit_decision();
    test_damerau_levenshtein_experimental();
    test_english_word_protection();
    test_password_context_policy();
    test_fake_backspace_and_coreldraw_compatibility();

    std::cout << "\n========================================" << std::endl;
    std::cout << " TESTS SUMMARY: " << std::endl;
    std::cout << "   PASSED: " << g_tests_passed << std::endl;
    std::cout << "   FAILED: " << g_tests_failed << std::endl;
    std::cout << "========================================" << std::endl;

    return g_tests_failed > 0 ? 1 : 0;
}
