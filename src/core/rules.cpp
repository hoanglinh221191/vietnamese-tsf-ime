#include "rules.hpp"
#include <cwctype>
#include <vector>

namespace vn_ime::core::rules {

wchar_t ToLower(wchar_t c) {
    if (c >= L'A' && c <= L'Z') return c - L'A' + L'a';
    switch (c) {
        case L'Á': return L'á'; case L'À': return L'à'; case L'Ả': return L'ả'; case L'Ã': return L'ã'; case L'Ạ': return L'ạ';
        case L'Ă': return L'ă'; case L'Ắ': return L'ắ'; case L'Ằ': return L'ằ'; case L'Ẳ': return L'ẳ'; case L'Ẵ': return L'ẵ'; case L'Ặ': return L'ặ';
        case L'Â': return L'â'; case L'Ấ': return L'ấ'; case L'Ầ': return L'ầ'; case L'Ẩ': return L'ẩ'; case L'Ẫ': return L'ẫ'; case L'Ậ': return L'ậ';
        case L'É': return L'é'; case L'È': return L'è'; case L'Ẻ': return L'ẻ'; case L'Ẽ': return L'ẽ'; case L'Ẹ': return L'ẹ';
        case L'Ê': return L'ê'; case L'Ế': return L'ế'; case L'Ề': return L'ề'; case L'Ể': return L'ể'; case L'Ễ': return L'ễ'; case L'Ệ': return L'ệ';
        case L'Í': return L'í'; case L'Ì': return L'ì'; case L'Ỉ': return L'ỉ'; case L'Ĩ': return L'ĩ'; case L'Ị': return L'ị';
        case L'Ó': return L'ó'; case L'Ò': return L'ò'; case L'Ỏ': return L'ỏ'; case L'Õ': return L'õ'; case L'Ọ': return L'ọ';
        case L'Ô': return L'ô'; case L'Ố': return L'ố'; case L'Ồ': return L'ồ'; case L'Ổ': return L'ổ'; case L'Ỗ': return L'ỗ'; case L'Ộ': return L'ộ';
        case L'Ơ': return L'ơ'; case L'Ớ': return L'ớ'; case L'Ờ': return L'ờ'; case L'Ở': return L'ở'; case L'Ỡ': return L'ỡ'; case L'Ợ': return L'ợ';
        case L'Ú': return L'ú'; case L'Ù': return L'ù'; case L'Ủ': return L'ủ'; case L'Ũ': return L'ũ'; case L'Ụ': return L'ụ';
        case L'Ư': return L'ư'; case L'Ứ': return L'ứ'; case L'Ừ': return L'ừ'; case L'Ử': return L'ử'; case L'Ữ': return L'ữ'; case L'Ự': return L'ự';
        case L'Ý': return L'ý'; case L'Ỳ': return L'ỳ'; case L'Ỷ': return L'ỷ'; case L'Ỹ': return L'ỹ'; case L'Ỵ': return L'ỵ';
        case L'Đ': return L'đ';
    }
    return c;
}

wchar_t ToUpper(wchar_t c) {
    if (c >= L'a' && c <= L'z') return c - L'a' + L'A';
    switch (c) {
        case L'á': return L'Á'; case L'à': return L'À'; case L'ả': return L'Ả'; case L'ã': return L'Ã'; case L'ạ': return L'Ạ';
        case L'ă': return L'Ă'; case L'ắ': return L'Ắ'; case L'ằ': return L'Ằ'; case L'ẳ': return L'Ẳ'; case L'ẵ': return L'Ẵ'; case L'ặ': return L'Ặ';
        case L'â': return L'Â'; case L'ấ': return L'Ấ'; case L'ầ': return L'Ầ'; case L'ẩ': return L'Ẩ'; case L'ẫ': return L'Ẫ'; case L'ậ': return L'Ậ';
        case L'é': return L'É'; case L'è': return L'È'; case L'ẻ': return L'Ẻ'; case L'ẽ': return L'Ẽ'; case L'ẹ': return L'Ẹ';
        case L'ê': return L'Ê'; case L'ế': return L'Ế'; case L'ề': return L'Ề'; case L'ể': return L'Ể'; case L'ễ': return L'Ễ'; case L'ệ': return L'Ệ';
        case L'í': return L'Í'; case L'ì': return L'Ì'; case L'ỉ': return L'Ỉ'; case L'ĩ': return L'Ĩ'; case L'ị': return L'Ị';
        case L'ó': return L'Ó'; case L'ò': return L'Ò'; case L'ỏ': return L'Ỏ'; case L'õ': return L'Õ'; case L'ọ': return L'Ọ';
        case L'ô': return L'Ô'; case L'ố': return L'Ố'; case L'ồ': return L'Ồ'; case L'ổ': return L'Ổ'; case L'ỗ': return L'Ỗ'; case L'ộ': return L'Ộ';
        case L'ơ': return L'Ơ'; case L'ớ': return L'Ớ'; case L'ờ': return L'Ờ'; case L'ở': return L'Ở'; case L'ỡ': return L'Ỡ'; case L'ợ': return L'Ợ';
        case L'ú': return L'Ú'; case L'ù': return L'Ù'; case L'ủ': return L'Ủ'; case L'ũ': return L'Ũ'; case L'ụ': return L'Ụ';
        case L'ư': return L'Ư'; case L'ứ': return L'Ứ'; case L'ừ': return L'Ừ'; case L'ử': return L'Ử'; case L'ữ': return L'Ữ'; case L'ự': return L'Ự';
        case L'ý': return L'Ý'; case L'ỳ': return L'Ỳ'; case L'ỷ': return L'Ỷ'; case L'ỹ': return L'Ỹ'; case L'ỵ': return L'Ỵ';
        case L'đ': return L'Đ';
    }
    return c;
}

bool GetVowelData(wchar_t c, VowelData& data) {
    switch (c) {
        // Lowercase a
        case L'a': data = {L'a', L'a', ToneMark::None, false}; return true;
        case L'á': data = {L'a', L'a', ToneMark::Sacute, false}; return true;
        case L'à': data = {L'a', L'a', ToneMark::Grave, false}; return true;
        case L'ả': data = {L'a', L'a', ToneMark::Hook, false}; return true;
        case L'ã': data = {L'a', L'a', ToneMark::Tilde, false}; return true;
        case L'ạ': data = {L'a', L'a', ToneMark::Dot, false}; return true;
        
        case L'ă': data = {L'a', L'ă', ToneMark::None, false}; return true;
        case L'ắ': data = {L'a', L'ă', ToneMark::Sacute, false}; return true;
        case L'ằ': data = {L'a', L'ă', ToneMark::Grave, false}; return true;
        case L'ẳ': data = {L'a', L'ă', ToneMark::Hook, false}; return true;
        case L'ẵ': data = {L'a', L'ă', ToneMark::Tilde, false}; return true;
        case L'ặ': data = {L'a', L'ă', ToneMark::Dot, false}; return true;

        case L'â': data = {L'a', L'â', ToneMark::None, false}; return true;
        case L'ấ': data = {L'a', L'â', ToneMark::Sacute, false}; return true;
        case L'ầ': data = {L'a', L'â', ToneMark::Grave, false}; return true;
        case L'ẩ': data = {L'a', L'â', ToneMark::Hook, false}; return true;
        case L'ẫ': data = {L'a', L'â', ToneMark::Tilde, false}; return true;
        case L'ậ': data = {L'a', L'â', ToneMark::Dot, false}; return true;

        // Uppercase A
        case L'A': data = {L'a', L'a', ToneMark::None, true}; return true;
        case L'Á': data = {L'a', L'a', ToneMark::Sacute, true}; return true;
        case L'À': data = {L'a', L'a', ToneMark::Grave, true}; return true;
        case L'Ả': data = {L'a', L'a', ToneMark::Hook, true}; return true;
        case L'Ã': data = {L'a', L'a', ToneMark::Tilde, true}; return true;
        case L'Ạ': data = {L'a', L'a', ToneMark::Dot, true}; return true;

        case L'Ă': data = {L'a', L'ă', ToneMark::None, true}; return true;
        case L'Ắ': data = {L'a', L'ă', ToneMark::Sacute, true}; return true;
        case L'Ằ': data = {L'a', L'ă', ToneMark::Grave, true}; return true;
        case L'Ẳ': data = {L'a', L'ă', ToneMark::Hook, true}; return true;
        case L'Ẵ': data = {L'a', L'ă', ToneMark::Tilde, true}; return true;
        case L'Ặ': data = {L'a', L'ă', ToneMark::Dot, true}; return true;

        case L'Â': data = {L'a', L'â', ToneMark::None, true}; return true;
        case L'Ấ': data = {L'a', L'â', ToneMark::Sacute, true}; return true;
        case L'Ầ': data = {L'a', L'â', ToneMark::Grave, true}; return true;
        case L'Ẩ': data = {L'a', L'â', ToneMark::Hook, true}; return true;
        case L'Ẫ': data = {L'a', L'â', ToneMark::Tilde, true}; return true;
        case L'Ậ': data = {L'a', L'â', ToneMark::Dot, true}; return true;

        // Lowercase e
        case L'e': data = {L'e', L'e', ToneMark::None, false}; return true;
        case L'é': data = {L'e', L'e', ToneMark::Sacute, false}; return true;
        case L'è': data = {L'e', L'e', ToneMark::Grave, false}; return true;
        case L'ẻ': data = {L'e', L'e', ToneMark::Hook, false}; return true;
        case L'ẽ': data = {L'e', L'e', ToneMark::Tilde, false}; return true;
        case L'ẹ': data = {L'e', L'e', ToneMark::Dot, false}; return true;

        case L'ê': data = {L'e', L'ê', ToneMark::None, false}; return true;
        case L'ế': data = {L'e', L'ê', ToneMark::Sacute, false}; return true;
        case L'ề': data = {L'e', L'ê', ToneMark::Grave, false}; return true;
        case L'ể': data = {L'e', L'ê', ToneMark::Hook, false}; return true;
        case L'ễ': data = {L'e', L'ê', ToneMark::Tilde, false}; return true;
        case L'ệ': data = {L'e', L'ê', ToneMark::Dot, false}; return true;

        // Uppercase E
        case L'E': data = {L'e', L'e', ToneMark::None, true}; return true;
        case L'É': data = {L'e', L'e', ToneMark::Sacute, true}; return true;
        case L'È': data = {L'e', L'e', ToneMark::Grave, true}; return true;
        case L'Ẻ': data = {L'e', L'e', ToneMark::Hook, true}; return true;
        case L'Ẽ': data = {L'e', L'e', ToneMark::Tilde, true}; return true;
        case L'Ẹ': data = {L'e', L'e', ToneMark::Dot, true}; return true;

        case L'Ê': data = {L'e', L'ê', ToneMark::None, true}; return true;
        case L'Ế': data = {L'e', L'ê', ToneMark::Sacute, true}; return true;
        case L'Ề': data = {L'e', L'ê', ToneMark::Grave, true}; return true;
        case L'Ể': data = {L'e', L'ê', ToneMark::Hook, true}; return true;
        case L'Ễ': data = {L'e', L'ê', ToneMark::Tilde, true}; return true;
        case L'Ệ': data = {L'e', L'ê', ToneMark::Dot, true}; return true;

        // Lowercase i
        case L'i': data = {L'i', L'i', ToneMark::None, false}; return true;
        case L'í': data = {L'i', L'i', ToneMark::Sacute, false}; return true;
        case L'ì': data = {L'i', L'i', ToneMark::Grave, false}; return true;
        case L'ỉ': data = {L'i', L'i', ToneMark::Hook, false}; return true;
        case L'ĩ': data = {L'i', L'i', ToneMark::Tilde, false}; return true;
        case L'ị': data = {L'i', L'i', ToneMark::Dot, false}; return true;

        // Uppercase I
        case L'I': data = {L'i', L'i', ToneMark::None, true}; return true;
        case L'Í': data = {L'i', L'i', ToneMark::Sacute, true}; return true;
        case L'Ì': data = {L'i', L'i', ToneMark::Grave, true}; return true;
        case L'Ỉ': data = {L'i', L'i', ToneMark::Hook, true}; return true;
        case L'Ĩ': data = {L'i', L'i', ToneMark::Tilde, true}; return true;
        case L'Ị': data = {L'i', L'i', ToneMark::Dot, true}; return true;

        // Lowercase o
        case L'o': data = {L'o', L'o', ToneMark::None, false}; return true;
        case L'ó': data = {L'o', L'o', ToneMark::Sacute, false}; return true;
        case L'ò': data = {L'o', L'o', ToneMark::Grave, false}; return true;
        case L'ỏ': data = {L'o', L'o', ToneMark::Hook, false}; return true;
        case L'õ': data = {L'o', L'o', ToneMark::Tilde, false}; return true;
        case L'ọ': data = {L'o', L'o', ToneMark::Dot, false}; return true;

        case L'ô': data = {L'o', L'ô', ToneMark::None, false}; return true;
        case L'ố': data = {L'o', L'ô', ToneMark::Sacute, false}; return true;
        case L'ồ': data = {L'o', L'ô', ToneMark::Grave, false}; return true;
        case L'ổ': data = {L'o', L'ô', ToneMark::Hook, false}; return true;
        case L'ỗ': data = {L'o', L'ô', ToneMark::Tilde, false}; return true;
        case L'ộ': data = {L'o', L'ô', ToneMark::Dot, false}; return true;

        case L'ơ': data = {L'o', L'ơ', ToneMark::None, false}; return true;
        case L'ớ': data = {L'o', L'ơ', ToneMark::Sacute, false}; return true;
        case L'ờ': data = {L'o', L'ơ', ToneMark::Grave, false}; return true;
        case L'ở': data = {L'o', L'ơ', ToneMark::Hook, false}; return true;
        case L'ỡ': data = {L'o', L'ơ', ToneMark::Tilde, false}; return true;
        case L'ợ': data = {L'o', L'ơ', ToneMark::Dot, false}; return true;

        // Uppercase O
        case L'O': data = {L'o', L'o', ToneMark::None, true}; return true;
        case L'Ó': data = {L'o', L'o', ToneMark::Sacute, true}; return true;
        case L'Ò': data = {L'o', L'o', ToneMark::Grave, true}; return true;
        case L'Ỏ': data = {L'o', L'o', ToneMark::Hook, true}; return true;
        case L'Õ': data = {L'o', L'o', ToneMark::Tilde, true}; return true;
        case L'Ọ': data = {L'o', L'o', ToneMark::Dot, true}; return true;

        case L'Ô': data = {L'o', L'ô', ToneMark::None, true}; return true;
        case L'Ố': data = {L'o', L'ô', ToneMark::Sacute, true}; return true;
        case L'Ồ': data = {L'o', L'ô', ToneMark::Grave, true}; return true;
        case L'Ổ': data = {L'o', L'ô', ToneMark::Hook, true}; return true;
        case L'Ỗ': data = {L'o', L'ô', ToneMark::Tilde, true}; return true;
        case L'Ộ': data = {L'o', L'ô', ToneMark::Dot, true}; return true;

        case L'Ơ': data = {L'o', L'ơ', ToneMark::None, true}; return true;
        case L'Ớ': data = {L'o', L'ơ', ToneMark::Sacute, true}; return true;
        case L'Ờ': data = {L'o', L'ơ', ToneMark::Grave, true}; return true;
        case L'Ở': data = {L'o', L'ơ', ToneMark::Hook, true}; return true;
        case L'Ỡ': data = {L'o', L'ơ', ToneMark::Tilde, true}; return true;
        case L'Ợ': data = {L'o', L'ơ', ToneMark::Dot, true}; return true;

        // Lowercase u
        case L'u': data = {L'u', L'u', ToneMark::None, false}; return true;
        case L'ú': data = {L'u', L'u', ToneMark::Sacute, false}; return true;
        case L'ù': data = {L'u', L'u', ToneMark::Grave, false}; return true;
        case L'ủ': data = {L'u', L'u', ToneMark::Hook, false}; return true;
        case L'ũ': data = {L'u', L'u', ToneMark::Tilde, false}; return true;
        case L'ụ': data = {L'u', L'u', ToneMark::Dot, false}; return true;

        case L'ư': data = {L'u', L'ư', ToneMark::None, false}; return true;
        case L'ứ': data = {L'u', L'ư', ToneMark::Sacute, false}; return true;
        case L'ừ': data = {L'u', L'ư', ToneMark::Grave, false}; return true;
        case L'ử': data = {L'u', L'ư', ToneMark::Hook, false}; return true;
        case L'ữ': data = {L'u', L'ư', ToneMark::Tilde, false}; return true;
        case L'ự': data = {L'u', L'ư', ToneMark::Dot, false}; return true;

        // Uppercase U
        case L'U': data = {L'u', L'u', ToneMark::None, true}; return true;
        case L'Ú': data = {L'u', L'u', ToneMark::Sacute, true}; return true;
        case L'Ù': data = {L'u', L'u', ToneMark::Grave, true}; return true;
        case L'Ủ': data = {L'u', L'u', ToneMark::Hook, true}; return true;
        case L'Ũ': data = {L'u', L'u', ToneMark::Tilde, true}; return true;
        case L'Ụ': data = {L'u', L'u', ToneMark::Dot, true}; return true;

        case L'Ư': data = {L'u', L'ư', ToneMark::None, true}; return true;
        case L'Ứ': data = {L'u', L'ư', ToneMark::Sacute, true}; return true;
        case L'Ừ': data = {L'u', L'ư', ToneMark::Grave, true}; return true;
        case L'Ử': data = {L'u', L'ư', ToneMark::Hook, true}; return true;
        case L'Ữ': data = {L'u', L'ư', ToneMark::Tilde, true}; return true;
        case L'Ự': data = {L'u', L'ư', ToneMark::Dot, true}; return true;

        // Lowercase y
        case L'y': data = {L'y', L'y', ToneMark::None, false}; return true;
        case L'ý': data = {L'y', L'y', ToneMark::Sacute, false}; return true;
        case L'ỳ': data = {L'y', L'y', ToneMark::Grave, false}; return true;
        case L'ỷ': data = {L'y', L'y', ToneMark::Hook, false}; return true;
        case L'ỹ': data = {L'y', L'y', ToneMark::Tilde, false}; return true;
        case L'ỵ': data = {L'y', L'y', ToneMark::Dot, false}; return true;

        // Uppercase Y
        case L'Y': data = {L'y', L'y', ToneMark::None, true}; return true;
        case L'Ý': data = {L'y', L'y', ToneMark::Sacute, true}; return true;
        case L'Ỳ': data = {L'y', L'y', ToneMark::Grave, true}; return true;
        case L'Ỷ': data = {L'y', L'y', ToneMark::Hook, true}; return true;
        case L'Ỹ': data = {L'y', L'y', ToneMark::Tilde, true}; return true;
        case L'Ỵ': data = {L'y', L'y', ToneMark::Dot, true}; return true;
    }
    return false;
}

wchar_t MakeVowel(wchar_t raw, ToneMark tone, bool is_upper) {
    wchar_t c = L'\0';
    wchar_t r = ToLower(raw);
    switch (r) {
        case L'a':
            switch (tone) {
                case ToneMark::None: c = L'a'; break;
                case ToneMark::Sacute: c = L'á'; break;
                case ToneMark::Grave: c = L'à'; break;
                case ToneMark::Hook: c = L'ả'; break;
                case ToneMark::Tilde: c = L'ã'; break;
                case ToneMark::Dot: c = L'ạ'; break;
            }
            break;
        case L'ă':
            switch (tone) {
                case ToneMark::None: c = L'ă'; break;
                case ToneMark::Sacute: c = L'ắ'; break;
                case ToneMark::Grave: c = L'ằ'; break;
                case ToneMark::Hook: c = L'ẳ'; break;
                case ToneMark::Tilde: c = L'ẵ'; break;
                case ToneMark::Dot: c = L'ặ'; break;
            }
            break;
        case L'â':
            switch (tone) {
                case ToneMark::None: c = L'â'; break;
                case ToneMark::Sacute: c = L'ấ'; break;
                case ToneMark::Grave: c = L'ầ'; break;
                case ToneMark::Hook: c = L'ẩ'; break;
                case ToneMark::Tilde: c = L'ẫ'; break;
                case ToneMark::Dot: c = L'ậ'; break;
            }
            break;
        case L'e':
            switch (tone) {
                case ToneMark::None: c = L'e'; break;
                case ToneMark::Sacute: c = L'é'; break;
                case ToneMark::Grave: c = L'è'; break;
                case ToneMark::Hook: c = L'ẻ'; break;
                case ToneMark::Tilde: c = L'ẽ'; break;
                case ToneMark::Dot: c = L'ẹ'; break;
            }
            break;
        case L'ê':
            switch (tone) {
                case ToneMark::None: c = L'ê'; break;
                case ToneMark::Sacute: c = L'ế'; break;
                case ToneMark::Grave: c = L'ề'; break;
                case ToneMark::Hook: c = L'ể'; break;
                case ToneMark::Tilde: c = L'ễ'; break;
                case ToneMark::Dot: c = L'ệ'; break;
            }
            break;
        case L'i':
            switch (tone) {
                case ToneMark::None: c = L'i'; break;
                case ToneMark::Sacute: c = L'í'; break;
                case ToneMark::Grave: c = L'ì'; break;
                case ToneMark::Hook: c = L'ỉ'; break;
                case ToneMark::Tilde: c = L'ĩ'; break;
                case ToneMark::Dot: c = L'ị'; break;
            }
            break;
        case L'o':
            switch (tone) {
                case ToneMark::None: c = L'o'; break;
                case ToneMark::Sacute: c = L'ó'; break;
                case ToneMark::Grave: c = L'ò'; break;
                case ToneMark::Hook: c = L'ỏ'; break;
                case ToneMark::Tilde: c = L'õ'; break;
                case ToneMark::Dot: c = L'ọ'; break;
            }
            break;
        case L'ô':
            switch (tone) {
                case ToneMark::None: c = L'ô'; break;
                case ToneMark::Sacute: c = L'ố'; break;
                case ToneMark::Grave: c = L'ồ'; break;
                case ToneMark::Hook: c = L'ổ'; break;
                case ToneMark::Tilde: c = L'ỗ'; break;
                case ToneMark::Dot: c = L'ộ'; break;
            }
            break;
        case L'ơ':
            switch (tone) {
                case ToneMark::None: c = L'ơ'; break;
                case ToneMark::Sacute: c = L'ớ'; break;
                case ToneMark::Grave: c = L'ờ'; break;
                case ToneMark::Hook: c = L'ở'; break;
                case ToneMark::Tilde: c = L'ỡ'; break;
                case ToneMark::Dot: c = L'ợ'; break;
            }
            break;
        case L'u':
            switch (tone) {
                case ToneMark::None: c = L'u'; break;
                case ToneMark::Sacute: c = L'ú'; break;
                case ToneMark::Grave: c = L'ù'; break;
                case ToneMark::Hook: c = L'ủ'; break;
                case ToneMark::Tilde: c = L'ũ'; break;
                case ToneMark::Dot: c = L'ụ'; break;
            }
            break;
        case L'ư':
            switch (tone) {
                case ToneMark::None: c = L'ư'; break;
                case ToneMark::Sacute: c = L'ứ'; break;
                case ToneMark::Grave: c = L'ừ'; break;
                case ToneMark::Hook: c = L'ử'; break;
                case ToneMark::Tilde: c = L'ữ'; break;
                case ToneMark::Dot: c = L'ự'; break;
            }
            break;
        case L'y':
            switch (tone) {
                case ToneMark::None: c = L'y'; break;
                case ToneMark::Sacute: c = L'ý'; break;
                case ToneMark::Grave: c = L'ỳ'; break;
                case ToneMark::Hook: c = L'ỷ'; break;
                case ToneMark::Tilde: c = L'ỹ'; break;
                case ToneMark::Dot: c = L'ỵ'; break;
            }
            break;
    }
    return is_upper ? ToUpper(c) : c;
}

bool IsVowel(wchar_t c) {
    VowelData vd;
    return GetVowelData(c, vd);
}

bool IsConsonant(wchar_t c) {
    if (!std::iswalpha(static_cast<wint_t>(c))) return false;
    return !IsVowel(c);
}

int FindTonePosition(std::wstring_view word) {
    struct Indices {
        size_t arr[16];
        size_t count = 0;
    } idxs;

    bool has_qu = false;
    bool has_gi = false;

    if (word.length() >= 2) {
        wchar_t first = ToLower(word[0]);
        wchar_t second = ToLower(word[1]);
        if (first == L'q' && second == L'u') {
            has_qu = true;
        } else if (first == L'g' && second == L'i') {
            has_gi = true;
        }
    }

    for (size_t i = 0; i < word.length(); ++i) {
        if (IsVowel(word[i])) {
            idxs.arr[idxs.count++] = i;
            if (idxs.count >= 16) break;
        }
    }

    if (idxs.count == 0) return -1;

    if (has_qu && idxs.count > 1 && idxs.arr[0] == 1) {
        for (size_t i = 1; i < idxs.count; ++i) {
            idxs.arr[i - 1] = idxs.arr[i];
        }
        idxs.count--;
    } else if (has_gi && idxs.count > 1 && idxs.arr[0] == 1) {
        for (size_t i = 1; i < idxs.count; ++i) {
            idxs.arr[i - 1] = idxs.arr[i];
        }
        idxs.count--;
    }

    if (idxs.count == 0) {
        return 1; // Fallback to index 1 ('u' in 'qu' or 'i' in 'gi' if it's the only one)
    }

    if (idxs.count == 1) {
        return static_cast<int>(idxs.arr[0]);
    }

    if (idxs.count == 2) {
        bool has_final = (idxs.arr[1] < word.length() - 1);
        
        wchar_t v0 = ToLower(word[idxs.arr[0]]);
        wchar_t v1 = ToLower(word[idxs.arr[1]]);
        VowelData vd0, vd1;
        GetVowelData(v0, vd0);
        GetVowelData(v1, vd1);
        
        bool is_ia_ua_ua = (vd0.raw == L'i' && vd1.raw == L'a') ||
                           (vd0.raw == L'u' && vd1.raw == L'a') ||
                           (vd0.raw == L'ư' && vd1.raw == L'a');

        bool is_uy = (vd0.raw == L'u' && vd1.raw == L'y');

        if (is_ia_ua_ua) {
            return static_cast<int>(idxs.arr[0]);
        }
        if (is_uy) {
            return static_cast<int>(idxs.arr[1]);
        }
        if (has_final) {
            return static_cast<int>(idxs.arr[1]);
        } else {
            return static_cast<int>(idxs.arr[0]);
        }
    }

    // count >= 3
    if (idxs.count == 3) {
        wchar_t v0 = ToLower(word[idxs.arr[0]]);
        wchar_t v1 = ToLower(word[idxs.arr[1]]);
        wchar_t v2 = ToLower(word[idxs.arr[2]]);
        VowelData vd0, vd1, vd2;
        GetVowelData(v0, vd0);
        GetVowelData(v1, vd1);
        GetVowelData(v2, vd2);
        
        bool is_uya_uye = (vd0.raw == L'u' && vd1.raw == L'y' && (vd2.raw == L'a' || vd2.raw == L'ê'));
        if (is_uya_uye) {
            return static_cast<int>(idxs.arr[2]);
        }
    }
    return static_cast<int>(idxs.arr[1]);
}

std::wstring ApplyTone(std::wstring_view word, ToneMark tone) {
    std::wstring result(word);
    
    for (size_t i = 0; i < result.length(); ++i) {
        VowelData vd;
        if (GetVowelData(result[i], vd)) {
            result[i] = MakeVowel(vd.raw, ToneMark::None, vd.is_upper);
        }
    }

    if (tone == ToneMark::None) {
        return result;
    }

    int tone_pos = FindTonePosition(result);
    if (tone_pos != -1) {
        VowelData vd;
        if (GetVowelData(result[tone_pos], vd)) {
            result[tone_pos] = MakeVowel(vd.raw, tone, vd.is_upper);
        }
    }
    return result;
}

bool IsModificationKey(wchar_t ch, InputMethod method) {
    wchar_t lch = ToLower(ch);
    if (method == InputMethod::Telex || method == InputMethod::SimpleTelex) {
        return (lch == L'w' || lch == L'a' || lch == L'e' || lch == L'o' || lch == L'd');
    } else if (method == InputMethod::VNI) {
        return (lch >= L'6' && lch <= L'9');
    }
    return false;
}

bool ApplyModification(std::wstring& word, wchar_t modKey, InputMethod method) {
    wchar_t lkey = ToLower(modKey);
    bool modified = false;

    if (method == InputMethod::Telex || method == InputMethod::SimpleTelex) {
        if (lkey == L'd') {
            if (word.empty()) return false;
            wchar_t last = word.back();
            if (last == L'd') { word.back() = L'đ'; modified = true; }
            else if (last == L'D') { word.back() = L'Đ'; modified = true; }
            else if (last == L'đ') { word.back() = L'd'; modified = true; }
            else if (last == L'Đ') { word.back() = L'D'; modified = true; }
        }
        else if (lkey == L'a' || lkey == L'e' || lkey == L'o') {
            // Telex modification for duplicate vowel: aa->â, ee->ê, oo->ô
            if (word.empty()) return false;
            
            // Find the last vowel
            int last_vowel_pos = -1;
            for (int i = static_cast<int>(word.length()) - 1; i >= 0; --i) {
                if (IsVowel(word[i])) {
                    last_vowel_pos = i;
                    break;
                }
            }
            if (last_vowel_pos == -1) return false;

            VowelData vd;
            GetVowelData(word[last_vowel_pos], vd);

            if (lkey == L'a' && vd.base == L'a') {
                if (vd.raw == L'a' || vd.raw == L'ă') {
                    word[last_vowel_pos] = MakeVowel(L'â', vd.tone, vd.is_upper);
                    modified = true;
                } else if (vd.raw == L'â') {
                    word[last_vowel_pos] = MakeVowel(L'a', vd.tone, vd.is_upper);
                    modified = true;
                }
            }
            else if (lkey == L'e' && vd.base == L'e') {
                if (vd.raw == L'e') {
                    word[last_vowel_pos] = MakeVowel(L'ê', vd.tone, vd.is_upper);
                    modified = true;
                } else if (vd.raw == L'ê') {
                    word[last_vowel_pos] = MakeVowel(L'e', vd.tone, vd.is_upper);
                    modified = true;
                }
            }
            else if (lkey == L'o' && vd.base == L'o') {
                if (vd.raw == L'o' || vd.raw == L'ơ') {
                    word[last_vowel_pos] = MakeVowel(L'ô', vd.tone, vd.is_upper);
                    modified = true;
                } else if (vd.raw == L'ô') {
                    word[last_vowel_pos] = MakeVowel(L'o', vd.tone, vd.is_upper);
                    modified = true;
                }
            }
        }
        else if (lkey == L'w') {
            // Find vowels
            bool has_u = false;
            bool has_o = false;
            bool has_a = false;
            
            size_t u_pos = 0, o_pos = 0, a_pos = 0;
            
            for (size_t i = 0; i < word.length(); ++i) {
                VowelData vd;
                if (GetVowelData(word[i], vd)) {
                    const bool is_qu_glide = i == 1 && ToLower(word[0]) == L'q';
                    if (vd.base == L'u' && !is_qu_glide && !has_u) { has_u = true; u_pos = i; }
                    else if (vd.base == L'o' && !has_o) { has_o = true; o_pos = i; }
                    else if (vd.base == L'a') { has_a = true; a_pos = i; }
                }
            }

            if (has_u && has_o) {
                // Check if already modified
                VowelData vdu, vdo;
                GetVowelData(word[u_pos], vdu);
                GetVowelData(word[o_pos], vdo);
                if (vdu.raw == L'ư' && vdo.raw == L'ơ') {
                    word[u_pos] = MakeVowel(L'u', vdu.tone, vdu.is_upper);
                    word[o_pos] = MakeVowel(L'o', vdo.tone, vdo.is_upper);
                } else {
                    word[u_pos] = MakeVowel(L'ư', vdu.tone, vdu.is_upper);
                    word[o_pos] = MakeVowel(L'ơ', vdo.tone, vdo.is_upper);
                }
                modified = true;
            }
            else if (has_u) {
                VowelData vdu;
                GetVowelData(word[u_pos], vdu);
                if (vdu.raw == L'ư') {
                    word[u_pos] = MakeVowel(L'u', vdu.tone, vdu.is_upper);
                } else {
                    word[u_pos] = MakeVowel(L'ư', vdu.tone, vdu.is_upper);
                }
                modified = true;
            }
            else if (has_o) {
                VowelData vdo;
                GetVowelData(word[o_pos], vdo);
                if (vdo.raw == L'ơ') {
                    word[o_pos] = MakeVowel(L'o', vdo.tone, vdo.is_upper);
                } else {
                    word[o_pos] = MakeVowel(L'ơ', vdo.tone, vdo.is_upper);
                }
                modified = true;
            }
            else if (has_a) {
                VowelData vda;
                GetVowelData(word[a_pos], vda);
                if (vda.raw == L'ă') {
                    word[a_pos] = MakeVowel(L'a', vda.tone, vda.is_upper);
                } else {
                    word[a_pos] = MakeVowel(L'ă', vda.tone, vda.is_upper);
                }
                modified = true;
            }
            else {
                // If last character is 'w' or 'W' (meaning they typed 'w' as a new word, which got appended in ProcessKey)
                if (!word.empty()) {
                    wchar_t last = word.back();
                    if (last == L'w') { word.back() = L'ư'; modified = true; }
                    else if (last == L'W') { word.back() = L'Ư'; modified = true; }
                }
            }
        }
    }
    else if (method == InputMethod::VNI) {
        if (word.empty()) return false;
        
        if (lkey == L'9') {
            wchar_t last = word.back();
            if (last == L'd') { word.back() = L'đ'; modified = true; }
            else if (last == L'D') { word.back() = L'Đ'; modified = true; }
            else if (last == L'đ') { word.back() = L'd'; modified = true; }
            else if (last == L'Đ') { word.back() = L'D'; modified = true; }
        }
        else {
            // VNI vowel modifications: 6 (circumflex), 7 (horn), 8 (breve)
            // Find the last vowel
            int last_vowel_pos = -1;
            for (int i = static_cast<int>(word.length()) - 1; i >= 0; --i) {
                if (IsVowel(word[i])) {
                    last_vowel_pos = i;
                    break;
                }
            }
            
            if (last_vowel_pos != -1) {
                VowelData vd;
                GetVowelData(word[last_vowel_pos], vd);

                if (lkey == L'6') {
                    if (vd.base == L'a' || vd.base == L'e' || vd.base == L'o') {
                        wchar_t target = (vd.base == L'a') ? L'â' : ((vd.base == L'e') ? L'ê' : L'ô');
                        if (vd.raw == target) {
                            word[last_vowel_pos] = MakeVowel(vd.base, vd.tone, vd.is_upper);
                        } else {
                            word[last_vowel_pos] = MakeVowel(target, vd.tone, vd.is_upper);
                        }
                        modified = true;
                    }
                }
                else if (lkey == L'7') {
                    // Check for u and o
                    bool has_u = false;
                    bool has_o = false;
                    size_t u_pos = 0, o_pos = 0;
                    for (size_t i = 0; i < word.length(); ++i) {
                        VowelData v;
                        if (GetVowelData(word[i], v)) {
                            const bool is_qu_glide = i == 1 && ToLower(word[0]) == L'q';
                            if (v.base == L'u' && !is_qu_glide && !has_u) { has_u = true; u_pos = i; }
                            else if (v.base == L'o' && !has_o) { has_o = true; o_pos = i; }
                        }
                    }

                    if (has_u && has_o) {
                        VowelData vdu, vdo;
                        GetVowelData(word[u_pos], vdu);
                        GetVowelData(word[o_pos], vdo);
                        if (vdu.raw == L'ư' && vdo.raw == L'ơ') {
                            word[u_pos] = MakeVowel(L'u', vdu.tone, vdu.is_upper);
                            word[o_pos] = MakeVowel(L'o', vdo.tone, vdo.is_upper);
                        } else {
                            word[u_pos] = MakeVowel(L'ư', vdu.tone, vdu.is_upper);
                            word[o_pos] = MakeVowel(L'ơ', vdo.tone, vdo.is_upper);
                        }
                        modified = true;
                    }
                    else if (has_u) {
                        VowelData vdu;
                        GetVowelData(word[u_pos], vdu);
                        if (vdu.raw == L'ư') {
                            word[u_pos] = MakeVowel(L'u', vdu.tone, vdu.is_upper);
                        } else {
                            word[u_pos] = MakeVowel(L'ư', vdu.tone, vdu.is_upper);
                        }
                        modified = true;
                    }
                    else if (has_o) {
                        VowelData vdo;
                        GetVowelData(word[o_pos], vdo);
                        if (vdo.raw == L'ơ') {
                            word[o_pos] = MakeVowel(L'o', vdo.tone, vdo.is_upper);
                        } else {
                            word[o_pos] = MakeVowel(L'ơ', vdo.tone, vdo.is_upper);
                        }
                        modified = true;
                    }
                }
                else if (lkey == L'8') {
                    if (vd.base == L'a') {
                        if (vd.raw == L'ă') {
                            word[last_vowel_pos] = MakeVowel(L'a', vd.tone, vd.is_upper);
                        } else {
                            word[last_vowel_pos] = MakeVowel(L'ă', vd.tone, vd.is_upper);
                        }
                        modified = true;
                    }
                }
            }
        }
    }

    return modified;
}

bool IsValidVietnameseChar(wchar_t c) {
    if (IsVowel(c)) return true;
    wchar_t lc = ToLower(c);
    return (lc == L'b' || lc == L'c' || lc == L'd' || lc == L'đ' || lc == L'g' ||
            lc == L'h' || lc == L'k' || lc == L'l' || lc == L'm' || lc == L'n' ||
            lc == L'p' || lc == L'q' || lc == L'r' || lc == L's' || lc == L't' ||
            lc == L'v' || lc == L'x');
}

static bool IsValidVowelGroup(std::wstring_view raw_vowels, bool in_progress) {
    const size_t num_vowels = raw_vowels.length();
    if (num_vowels == 0 || num_vowels > 3) return false;
    if (num_vowels == 1) return true;

    if (num_vowels == 2) {
        if (raw_vowels == L"ai" || raw_vowels == L"ao" || raw_vowels == L"au" || raw_vowels == L"ay" ||
            raw_vowels == L"âu" || raw_vowels == L"ây" || raw_vowels == L"eo" || raw_vowels == L"ia" ||
            raw_vowels == L"iê" || raw_vowels == L"iu" || raw_vowels == L"oa" || raw_vowels == L"oă" ||
            raw_vowels == L"oe" || raw_vowels == L"oi" || raw_vowels == L"ôi" || raw_vowels == L"ơi" ||
            raw_vowels == L"oo" || raw_vowels == L"ua" || raw_vowels == L"uâ" || raw_vowels == L"uô" ||
            raw_vowels == L"uơ" || raw_vowels == L"uê" || raw_vowels == L"ui" || raw_vowels == L"uy" ||
            raw_vowels == L"ưa" || raw_vowels == L"ươ" || raw_vowels == L"ưu" || raw_vowels == L"yê") {
            return true;
        }
        return in_progress &&
            (raw_vowels == L"uo" || raw_vowels == L"ue" || raw_vowels == L"ie" || raw_vowels == L"ye");
    }

    if (raw_vowels == L"iêu" || raw_vowels == L"yêu" || raw_vowels == L"oai" || raw_vowels == L"oao" ||
        raw_vowels == L"oay" || raw_vowels == L"oeo" || raw_vowels == L"uai" || raw_vowels == L"uây" ||
        raw_vowels == L"uôi" || raw_vowels == L"ươu" || raw_vowels == L"ươi" || raw_vowels == L"uya" ||
        raw_vowels == L"uyê") {
        return true;
    }
    return in_progress && raw_vowels == L"uye";
}

bool IsValidVietnamese(std::wstring_view word, bool in_progress) {
    if (word.empty()) return false;

    // Check all characters are valid Vietnamese characters (no f, j, w, z, digits, punctuation)
    for (wchar_t c : word) {
        if (!IsValidVietnameseChar(c)) {
            return false;
        }
    }

    // Find the first and last vowels
    int first_vowel = -1;
    int last_vowel = -1;
    for (int i = 0; i < static_cast<int>(word.length()); ++i) {
        if (IsVowel(word[i])) {
            if (first_vowel == -1) first_vowel = i;
            last_vowel = i;
        }
    }

    // Vietnamese syllable must have at least one vowel, or be a valid initial consonant group
    if (first_vowel == -1) {
        std::wstring lower_word(word);
        for (auto& c : lower_word) c = ToLower(c);
        return (lower_word == L"b" || lower_word == L"c" || lower_word == L"ch" || lower_word == L"d" ||
                lower_word == L"đ" || lower_word == L"g" || lower_word == L"gh" || lower_word == L"gi" ||
                lower_word == L"h" || lower_word == L"k" || lower_word == L"kh" || lower_word == L"l" ||
                lower_word == L"m" || lower_word == L"n" || lower_word == L"ng" || lower_word == L"ngh" ||
                lower_word == L"p" || lower_word == L"ph" || lower_word == L"q" || lower_word == L"r" ||
                lower_word == L"s" || lower_word == L"t" || lower_word == L"th" || lower_word == L"tr" ||
                lower_word == L"v" || lower_word == L"x");
    }

    // Vowels must be contiguous
    for (int i = first_vowel; i <= last_vowel; ++i) {
        if (!IsVowel(word[i])) return false;
    }

    // Extract prefix consonants, vowels, suffix consonants
    std::wstring initial(word.substr(0, first_vowel));
    std::wstring vowels(word.substr(first_vowel, last_vowel - first_vowel + 1));
    std::wstring final_cons(word.substr(last_vowel + 1));

    // Convert to lowercase for rules validation
    for (auto& c : initial) c = ToLower(c);
    for (auto& c : final_cons) c = ToLower(c);

    // Get raw vowel group (without tone, lowercase)
    std::wstring raw_vowels;
    ToneMark word_tone = ToneMark::None;
    for (wchar_t c : vowels) {
        VowelData vd;
        if (GetVowelData(c, vd)) {
            raw_vowels.push_back(vd.raw); // vd.raw is already lowercase raw vowel (e.g. 'a', 'â', 'ă')
            if (vd.tone != ToneMark::None) {
                if (word_tone != ToneMark::None && word_tone != vd.tone) {
                    return false; // Multiple different tones
                }
                word_tone = vd.tone;
            }
        }
    }

    // Prefer standard groups such as "iêu" in "giêu". Fall back to treating
    // "gi" as the onset only when that is required for forms such as "giưa".
    if (!IsValidVowelGroup(raw_vowels, in_progress) &&
        initial == L"g" && raw_vowels.length() > 1 && raw_vowels.front() == L'i') {
        std::wstring gi_vowels = raw_vowels.substr(1);
        if (IsValidVowelGroup(gi_vowels, in_progress)) {
            initial = L"gi";
            raw_vowels = std::move(gi_vowels);
        }
    }

    // Validate initial consonant group
    if (!initial.empty()) {
        if (initial != L"b" && initial != L"c" && initial != L"ch" && initial != L"d" &&
            initial != L"đ" && initial != L"g" && initial != L"gh" && initial != L"gi" &&
            initial != L"h" && initial != L"k" && initial != L"kh" && initial != L"l" &&
            initial != L"m" && initial != L"n" && initial != L"nh" && initial != L"ng" && initial != L"ngh" &&
            initial != L"p" && initial != L"ph" && initial != L"q" && initial != L"r" &&
            initial != L"s" && initial != L"t" && initial != L"th" && initial != L"tr" &&
            initial != L"v" && initial != L"x") {
            return false;
        }
    }

    // Validate final consonant group
    if (!final_cons.empty()) {
        if (final_cons != L"c" && final_cons != L"ch" && final_cons != L"m" &&
            final_cons != L"n" && final_cons != L"ng" && final_cons != L"nh" &&
            final_cons != L"p" && final_cons != L"t") {
            return false;
        }
    }

    // Detailed vowel-consonant combination spelling check
    if (!final_cons.empty() && !raw_vowels.empty()) {
        if (final_cons == L"nh" || final_cons == L"ch") {
            // Can ONLY follow: a, oa, i, ê, uê, uy
            if (raw_vowels != L"a" && raw_vowels != L"oa" && raw_vowels != L"i" &&
                raw_vowels != L"ê" && raw_vowels != L"uê" && raw_vowels != L"uy") {
                return false;
            }
        }
        else if (final_cons == L"ng" || final_cons == L"c") {
            // Cannot follow i, ê, y directly (as a single vowel)
            if (raw_vowels == L"i" || raw_vowels == L"ê" || raw_vowels == L"y") {
                return false;
            }
        }
        else if (final_cons == L"n" || final_cons == L"m") {
            // Cannot follow ư, y directly
            if (raw_vowels == L"ư" || raw_vowels == L"y") {
                return false;
            }
        }
        else if (final_cons == L"t" || final_cons == L"p") {
            // Cannot follow y directly
            if (raw_vowels == L"y") {
                return false;
            }
        }
    }

    // Rule for q: must be followed by u (except maybe if the whole word is just q, which is not a syllable anyway)
    if (initial == L"q") {
        if (raw_vowels.empty() || raw_vowels[0] != L'u') {
            return false;
        }
    }

    // Stop consonant tone rule: final consonant is c, ch, p, t -> tone must be Sacute (sắc) or Dot (nặng)
    if (final_cons == L"c" || final_cons == L"ch" || final_cons == L"p" || final_cons == L"t") {
        if (!in_progress && word_tone != ToneMark::Sacute && word_tone != ToneMark::Dot) {
            return false;
        }
    }

    if (!IsValidVowelGroup(raw_vowels, in_progress)) return false;

    // Front/Back vowel rules for initial consonants
    if (!initial.empty()) {
        wchar_t first_vowel_char = raw_vowels[0];
        bool is_front_vowel = (first_vowel_char == L'e' || first_vowel_char == L'ê' ||
                               first_vowel_char == L'i' || first_vowel_char == L'y');

        if (initial == L"gh" || initial == L"ngh") {
            // Must be front vowel
            if (first_vowel_char != L'e' && first_vowel_char != L'ê' && first_vowel_char != L'i') {
                return false;
            }
        }
        else if (initial == L"g" || initial == L"ng") {
            // Cannot be followed by e, ê, i (g can be followed by i as part of gi, but if initial is parsed as g and vowel is e/ê, it is invalid)
            if (first_vowel_char == L'e' || first_vowel_char == L'ê' || (initial == L"ng" && first_vowel_char == L'i')) {
                return false;
            }
        }
        else if (initial == L"k") {
            // Must be front vowel
            if (!is_front_vowel) {
                return false;
            }
        }
        else if (initial == L"c") {
            // Cannot be followed by e, ê, i, y
            if (is_front_vowel) {
                return false;
            }
        }
        else if (initial == L"p") {
            // Cannot be followed by â, ă, ư, ơ (only for plain loanwords like pa, pe, pi, po, pu, py)
            for (wchar_t v : raw_vowels) {
                if (v == L'â' || v == L'ă' || v == L'ư' || v == L'ơ') {
                    return false;
                }
            }
        }
    }

    return true;
}

bool IsToneKey(wchar_t ch, InputMethod method) {
    wchar_t lch = ToLower(ch);
    if (method == InputMethod::Telex || method == InputMethod::SimpleTelex) {
        return (lch == L's' || lch == L'f' || lch == L'r' || lch == L'x' || lch == L'j' || lch == L'z');
    } else if (method == InputMethod::VNI) {
        return (lch == L'1' || lch == L'2' || lch == L'3' || lch == L'4' || lch == L'5' || lch == L'0');
    }
    return false;
}

bool IsWordChar(wchar_t c) {
    wchar_t lc = ToLower(c);
    if (lc == L'đ') return true;
    return IsVowel(c) || IsConsonant(c);
}

std::optional<ReconversionSpan> ResolveReconversionSpan(
    std::wstring_view text,
    size_t selection_start,
    size_t selection_end,
    bool truncated_left,
    bool truncated_right) {
    if (selection_start > selection_end || selection_end > text.length()) {
        return std::nullopt;
    }

    size_t anchor_start = selection_start;
    size_t anchor_end = selection_end;
    if (selection_start == selection_end) {
        const size_t caret = selection_start;
        if (caret < text.length() && IsWordChar(text[caret])) {
            anchor_start = caret;
            anchor_end = caret + 1;
        } else if (caret > 0 && IsWordChar(text[caret - 1])) {
            anchor_start = caret - 1;
            anchor_end = caret;
        } else {
            return std::nullopt;
        }
    } else {
        for (size_t i = selection_start; i < selection_end; ++i) {
            if (!IsWordChar(text[i])) {
                return std::nullopt;
            }
        }
    }

    while (anchor_start > 0 && IsWordChar(text[anchor_start - 1])) {
        --anchor_start;
    }
    while (anchor_end < text.length() && IsWordChar(text[anchor_end])) {
        ++anchor_end;
    }

    if (anchor_start == anchor_end ||
        (anchor_start == 0 && truncated_left) ||
        (anchor_end == text.length() && truncated_right)) {
        return std::nullopt;
    }

    ReconversionSpan span;
    span.start = anchor_start;
    span.end = anchor_end;
    span.selection_start = selection_start;
    span.selection_end = selection_end;
    return span;
}

std::wstring ReconstructRawKeys(std::wstring_view word, InputMethod method) {
    std::wstring raw;
    std::vector<wchar_t> mods;
    bool has_u_horn = false;
    bool has_o_horn = false;

    for (wchar_t c : word) {
        VowelData vd;
        if (GetVowelData(c, vd)) {
            wchar_t vowel_char = MakeVowel(vd.raw, ToneMark::None, vd.is_upper);
            wchar_t base_char = vd.base;
            if (vd.is_upper) base_char = ToUpper(base_char);
            
            raw.push_back(base_char);
            
            if (method == InputMethod::Telex || method == InputMethod::SimpleTelex) {
                if (vowel_char == L'â' || vowel_char == L'Â') {
                    mods.push_back(L'a');
                } else if (vowel_char == L'ă' || vowel_char == L'Ă') {
                    mods.push_back(L'w');
                } else if (vowel_char == L'ê' || vowel_char == L'Ê') {
                    mods.push_back(L'e');
                } else if (vowel_char == L'ô' || vowel_char == L'Ô') {
                    mods.push_back(L'o');
                } else if (vowel_char == L'ơ' || vowel_char == L'Ơ') {
                    has_o_horn = true;
                } else if (vowel_char == L'ư' || vowel_char == L'Ư') {
                    has_u_horn = true;
                }
            } else if (method == InputMethod::VNI) {
                if (vowel_char == L'â' || vowel_char == L'Â') {
                    mods.push_back(L'6');
                } else if (vowel_char == L'ă' || vowel_char == L'Ă') {
                    mods.push_back(L'8');
                } else if (vowel_char == L'ê' || vowel_char == L'Ê') {
                    mods.push_back(L'6');
                } else if (vowel_char == L'ô' || vowel_char == L'Ô') {
                    mods.push_back(L'6');
                } else if (vowel_char == L'ơ' || vowel_char == L'Ơ') {
                    has_o_horn = true;
                } else if (vowel_char == L'ư' || vowel_char == L'Ư') {
                    has_u_horn = true;
                }
            }
        } else {
            wchar_t lch = ToLower(c);
            if (lch == L'đ') {
                raw.push_back(c == L'đ' ? L'd' : L'D');
                if (method == InputMethod::Telex || method == InputMethod::SimpleTelex) {
                    mods.push_back(L'd');
                } else if (method == InputMethod::VNI) {
                    mods.push_back(L'9');
                }
            } else {
                raw.push_back(c);
            }
        }
    }

    if (method == InputMethod::Telex || method == InputMethod::SimpleTelex) {
        if (has_u_horn || has_o_horn) {
            mods.push_back(L'w');
        }
    } else if (method == InputMethod::VNI) {
        if (has_u_horn || has_o_horn) {
            mods.push_back(L'7');
        }
    }

    for (wchar_t m : mods) {
        raw.push_back(m);
    }

    ToneMark tone = ToneMark::None;
    for (wchar_t c : word) {
        VowelData vd;
        if (GetVowelData(c, vd) && vd.tone != ToneMark::None) {
            tone = vd.tone;
            break;
        }
    }

    if (tone != ToneMark::None) {
        if (method == InputMethod::Telex || method == InputMethod::SimpleTelex) {
            if (tone == ToneMark::Sacute) raw.push_back(L's');
            else if (tone == ToneMark::Grave) raw.push_back(L'f');
            else if (tone == ToneMark::Hook) raw.push_back(L'r');
            else if (tone == ToneMark::Tilde) raw.push_back(L'x');
            else if (tone == ToneMark::Dot) raw.push_back(L'j');
        } else if (method == InputMethod::VNI) {
            if (tone == ToneMark::Sacute) raw.push_back(L'1');
            else if (tone == ToneMark::Grave) raw.push_back(L'2');
            else if (tone == ToneMark::Hook) raw.push_back(L'3');
            else if (tone == ToneMark::Tilde) raw.push_back(L'4');
            else if (tone == ToneMark::Dot) raw.push_back(L'5');
        }
    }

    return raw;
}

} // namespace vn_ime::core::rules
