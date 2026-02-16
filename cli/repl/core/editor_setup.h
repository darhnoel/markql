#pragma once

#include <string>

#include "repl/core/line_editor.h"
#include "repl/input/text_util.h"
#include "ui/color.h"

namespace xsql::cli {

inline constexpr char kPromptNormalPlain[] = "┌─(markql)\n└─▪ ";
inline constexpr char kPromptVimNormalPlain[] = "┌─(markql)[vim:normal]\n└─▪ ";
inline constexpr char kPromptVimInsertPlain[] = "┌─(markql)[vim:edit]\n└─▪ ";
inline constexpr char kPromptContinuationPlain[] = "... ";

inline constexpr size_t kPromptContinuationVisibleLen = sizeof(kPromptContinuationPlain) - 1;

inline size_t prompt_last_line_visible_width(const std::string& plain) {
  size_t line_start = plain.rfind('\n');
  line_start = (line_start == std::string::npos) ? 0 : line_start + 1;
  return column_width(plain, line_start, plain.size());
}

inline size_t normal_prompt_visible_len() {
  return prompt_last_line_visible_width(kPromptNormalPlain);
}

inline size_t vim_normal_prompt_visible_len() {
  return prompt_last_line_visible_width(kPromptVimNormalPlain);
}

inline size_t vim_insert_prompt_visible_len() {
  return prompt_last_line_visible_width(kPromptVimInsertPlain);
}

inline std::string make_prompt_text(const std::string& plain, bool color_enabled) {
  if (!color_enabled) return plain;
  return std::string(kColor.blue) + plain + kColor.reset;
}

inline std::string make_normal_repl_prompt(bool color_enabled) {
  return make_prompt_text(kPromptNormalPlain, color_enabled);
}

inline void configure_repl_editor(LineEditor& editor, bool color_enabled, bool highlight_enabled) {
  editor.set_mode_prompts(make_prompt_text(kPromptVimNormalPlain, color_enabled),
                          vim_normal_prompt_visible_len(),
                          make_prompt_text(kPromptVimInsertPlain, color_enabled),
                          vim_insert_prompt_visible_len());
  editor.set_keyword_color(color_enabled && highlight_enabled);
  editor.set_cont_prompt(kPromptContinuationPlain, kPromptContinuationVisibleLen);
}

}  // namespace xsql::cli
