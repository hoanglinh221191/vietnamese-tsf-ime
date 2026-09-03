#pragma once

#include <array>
#include <string_view>

#include "fuzzy_input.hpp"

namespace vn_ime::core::fuzzy_input::data {

struct DirectionalBigramRule {
    std::wstring_view source_previous;
    std::wstring_view source_current;
    std::wstring_view target_previous;
    std::wstring_view target_current;
    FuzzyInputFlag required_flag;
};

// Reviewed exceptions for real-word dialect errors. The shared bigram library
// may infer a target when at least one source token is invalid; when both
// source tokens are dictionary-valid, only this directional allowlist may
// authorize a rewrite.
inline constexpr auto kDirectionalBigramRules = std::to_array<DirectionalBigramRule>({
    // L and N
    {L"nàm", L"việc", L"làm", L"việc", FuzzyInputFlag::LAndN},
    {L"xin", L"nỗi", L"xin", L"lỗi", FuzzyInputFlag::LAndN},
    {L"nòng", L"nợn", L"lòng", L"lợn", FuzzyInputFlag::LAndN},
    {L"nòng", L"nơn", L"lòng", L"lợn", FuzzyInputFlag::LAndN},
    {L"lòng", L"nợn", L"lòng", L"lợn", FuzzyInputFlag::LAndN},
    {L"lòng", L"nơn", L"lòng", L"lợn", FuzzyInputFlag::LAndN},
    {L"lói", L"năng", L"nói", L"năng", FuzzyInputFlag::LAndN},
    {L"lói", L"chuyện", L"nói", L"chuyện", FuzzyInputFlag::LAndN},
    {L"no", L"lắng", L"lo", L"lắng", FuzzyInputFlag::LAndN},
    {L"no", L"sợ", L"lo", L"sợ", FuzzyInputFlag::LAndN},
    {L"lăng", L"lực", L"năng", L"lực", FuzzyInputFlag::LAndN},
    {L"lăng", L"lượng", L"năng", L"lượng", FuzzyInputFlag::LAndN},
    {L"lông", L"thôn", L"nông", L"thôn", FuzzyInputFlag::LAndN},
    {L"lông", L"dân", L"nông", L"dân", FuzzyInputFlag::LAndN},
    {L"lước", L"ngoài", L"nước", L"ngoài", FuzzyInputFlag::LAndN},
    {L"lước", L"mắt", L"nước", L"mắt", FuzzyInputFlag::LAndN},
    {L"lỗ", L"lực", L"nỗ", L"lực", FuzzyInputFlag::LAndN},
    {L"lặng", L"nề", L"nặng", L"nề", FuzzyInputFlag::LAndN},
    {L"nành", L"nghề", L"lành", L"nghề", FuzzyInputFlag::LAndN},
    {L"nập", L"tức", L"lập", L"tức", FuzzyInputFlag::LAndN},
    {L"nợi", L"ích", L"lợi", L"ích", FuzzyInputFlag::LAndN},
    {L"nưu", L"loát", L"lưu", L"loát", FuzzyInputFlag::LAndN},
    {L"lưu", L"noát", L"lưu", L"loát", FuzzyInputFlag::LAndN},
    {L"nực", L"lượng", L"lực", L"lượng", FuzzyInputFlag::LAndN},
    {L"lực", L"nượng", L"lực", L"lượng", FuzzyInputFlag::LAndN},

    // TR and CH
    {L"nha", L"chang", L"nha", L"trang", FuzzyInputFlag::TrAndCh},
    {L"chung", L"tâm", L"trung", L"tâm", FuzzyInputFlag::TrAndCh},
    {L"chưởng", L"thành", L"trưởng", L"thành", FuzzyInputFlag::TrAndCh},
    {L"trân", L"thành", L"chân", L"thành", FuzzyInputFlag::TrAndCh},
    {L"chân", L"trọng", L"trân", L"trọng", FuzzyInputFlag::TrAndCh},
    {L"trung", L"cư", L"chung", L"cư", FuzzyInputFlag::TrAndCh},
    {L"chiến", L"chanh", L"chiến", L"tranh", FuzzyInputFlag::TrAndCh},
    {L"chải", L"nghiệm", L"trải", L"nghiệm", FuzzyInputFlag::TrAndCh},
    {L"chương", L"chình", L"chương", L"trình", FuzzyInputFlag::TrAndCh},
    {L"trương", L"chình", L"chương", L"trình", FuzzyInputFlag::TrAndCh},
    {L"chung", L"thực", L"trung", L"thực", FuzzyInputFlag::TrAndCh},
    {L"chách", L"nhiệm", L"trách", L"nhiệm", FuzzyInputFlag::TrAndCh},
    {L"quan", L"chọng", L"quan", L"trọng", FuzzyInputFlag::TrAndCh},
    {L"trính", L"xác", L"chính", L"xác", FuzzyInputFlag::TrAndCh},
    {L"chang", L"trí", L"trang", L"trí", FuzzyInputFlag::TrAndCh},
    {L"chanh", L"chấp", L"tranh", L"chấp", FuzzyInputFlag::TrAndCh},

    // S and X
    {L"xinh", L"hoạt", L"sinh", L"hoạt", FuzzyInputFlag::SAndX},
    {L"xản", L"phẩm", L"sản", L"phẩm", FuzzyInputFlag::SAndX},
    {L"xơ", L"suất", L"sơ", L"suất", FuzzyInputFlag::SAndX},
    {L"sơ", L"xuất", L"sơ", L"suất", FuzzyInputFlag::SAndX},
    {L"sắp", L"sếp", L"sắp", L"xếp", FuzzyInputFlag::SAndX},
    {L"xắp", L"xếp", L"sắp", L"xếp", FuzzyInputFlag::SAndX},
    {L"sứ", L"sở", L"xứ", L"sở", FuzzyInputFlag::SAndX},
    {L"xuất", L"ăn", L"suất", L"ăn", FuzzyInputFlag::SAndX},
    {L"suất", L"sắc", L"xuất", L"sắc", FuzzyInputFlag::SAndX},
    {L"xuất", L"xắc", L"xuất", L"sắc", FuzzyInputFlag::SAndX},
    {L"xáng", L"tạo", L"sáng", L"tạo", FuzzyInputFlag::SAndX},
    {L"xinh", L"viên", L"sinh", L"viên", FuzzyInputFlag::SAndX},
    {L"xuy", L"nghĩ", L"suy", L"nghĩ", FuzzyInputFlag::SAndX},
    {L"sử", L"lý", L"xử", L"lý", FuzzyInputFlag::SAndX},
    {L"xâu", L"sắc", L"sâu", L"sắc", FuzzyInputFlag::SAndX},
    {L"sâu", L"xắc", L"sâu", L"sắc", FuzzyInputFlag::SAndX},
    {L"sông", L"pha", L"xông", L"pha", FuzzyInputFlag::SAndX},

    // R, D and GI
    {L"dữ", L"gìn", L"giữ", L"gìn", FuzzyInputFlag::RAndDAndGi},
    {L"rữ", L"gìn", L"giữ", L"gìn", FuzzyInputFlag::RAndDAndGi},
    {L"giành", L"dụm", L"dành", L"dụm", FuzzyInputFlag::RAndDAndGi},
    {L"dang", L"sơn", L"giang", L"sơn", FuzzyInputFlag::RAndDAndGi},
    {L"danh", L"giới", L"ranh", L"giới", FuzzyInputFlag::RAndDAndGi},
    {L"dải", L"quyết", L"giải", L"quyết", FuzzyInputFlag::RAndDAndGi},
    {L"dáo", L"dục", L"giáo", L"dục", FuzzyInputFlag::RAndDAndGi},
    {L"da", L"đình", L"gia", L"đình", FuzzyInputFlag::RAndDAndGi},
    {L"dõ", L"dàng", L"rõ", L"ràng", FuzzyInputFlag::RAndDAndGi},
    {L"giễ", L"giàng", L"dễ", L"dàng", FuzzyInputFlag::RAndDAndGi},
    {L"dễ", L"giàng", L"dễ", L"dàng", FuzzyInputFlag::RAndDAndGi},
    {L"giễ", L"dàng", L"dễ", L"dàng", FuzzyInputFlag::RAndDAndGi},
    {L"dản", L"dị", L"giản", L"dị", FuzzyInputFlag::RAndDAndGi},

    // Hook and Tilde (Hỏi / Ngã)
    {L"suy", L"nghỉ", L"suy", L"nghĩ", FuzzyInputFlag::HookAndTilde},
    {L"nghĩ", L"ngơi", L"nghỉ", L"ngơi", FuzzyInputFlag::HookAndTilde},
    {L"vấp", L"ngả", L"vấp", L"ngã", FuzzyInputFlag::HookAndTilde},
    {L"hướng", L"dẩn", L"hướng", L"dẫn", FuzzyInputFlag::HookAndTilde},
    {L"kỷ", L"thuật", L"kỹ", L"thuật", FuzzyInputFlag::HookAndTilde},
    {L"kỹ", L"luật", L"kỷ", L"luật", FuzzyInputFlag::HookAndTilde},
    {L"lảng", L"mạn", L"lãng", L"mạn", FuzzyInputFlag::HookAndTilde},
    {L"dủng", L"cảm", L"dũng", L"cảm", FuzzyInputFlag::HookAndTilde},
    {L"chuẫn", L"bị", L"chuẩn", L"bị", FuzzyInputFlag::HookAndTilde},
    {L"chia", L"sẽ", L"chia", L"sẻ", FuzzyInputFlag::HookAndTilde},
    {L"sửa", L"chửa", L"sửa", L"chữa", FuzzyInputFlag::HookAndTilde},
    {L"sữa", L"chữa", L"sửa", L"chữa", FuzzyInputFlag::HookAndTilde},
    {L"lảnh", L"đạo", L"lãnh", L"đạo", FuzzyInputFlag::HookAndTilde},
    {L"kiên", L"nhẩn", L"kiên", L"nhẫn", FuzzyInputFlag::HookAndTilde},
    {L"sẳn", L"sàng", L"sẵn", L"sàng", FuzzyInputFlag::HookAndTilde},
    {L"ngở", L"ngàng", L"ngỡ", L"ngàng", FuzzyInputFlag::HookAndTilde},
    {L"bở", L"ngở", L"bỡ", L"ngỡ", FuzzyInputFlag::HookAndTilde},
});

} // namespace vn_ime::core::fuzzy_input::data
