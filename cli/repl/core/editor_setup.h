#pragma once

#include <string>

#include "repl/core/line_editor.h"
#include "ui/color.h"

namespace xsql::cli {

inline constexpr char kPromptNormalPlain[] = "markql> ";
inline constexpr char kPromptVimNormalPlain[] = "markql (vim:normal)> ";
inline constexpr char kPromptVimInsertPlain[] = "markql (vim:edit)  > ";
inline constexpr char kPromptContinuationPlain[] = "... ";

inline constexpr size_t kPromptNormalVisibleLen = sizeof(kPromptNormalPlain) - 1;
inline constexpr size_t kPromptVimNormalVisibleLen = sizeof(kPromptVimNormalPlain) - 1;
inline constexpr size_t kPromptVimInsertVisibleLen = sizeof(kPromptVimInsertPlain) - 1;
inline constexpr size_t kPromptContinuationVisibleLen = sizeof(kPromptContinuationPlain) - 1;

inline std::string make_prompt_text(const std::string& plain, bool color_enabled) {
  if (!color_enabled) return plain;
  return std::string(kColor.blue) + plain + kColor.reset;
}

inline std::string make_normal_repl_prompt(bool color_enabled) {
  return make_prompt_text(kPromptNormalPlain, color_enabled);
}

inline void configure_repl_editor(LineEditor& editor, bool color_enabled, bool highlight_enabled) {
  editor.set_mode_prompts(make_prompt_text(kPromptVimNormalPlain, color_enabled),
                          kPromptVimNormalVisibleLen,
                          make_prompt_text(kPromptVimInsertPlain, color_enabled),
                          kPromptVimInsertVisibleLen);
  editor.set_keyword_color(color_enabled && highlight_enabled);
  editor.set_cont_prompt(kPromptContinuationPlain, kPromptContinuationVisibleLen);
}

}  // namespace xsql::cli
