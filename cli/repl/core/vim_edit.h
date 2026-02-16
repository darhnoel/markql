#pragma once

#include <cstddef>
#include <string>

namespace xsql::cli {

/// Moves forward by Vim word motions (`w`/`W`) from a byte cursor.
/// MUST treat count=0 as one step and clamp at buffer end.
size_t move_vim_word_forward_n(const std::string& buffer,
                               size_t pos,
                               size_t count,
                               bool big_word);

/// Moves backward by Vim word motions (`b`/`B`) from a byte cursor.
/// MUST treat count=0 as one step and clamp at buffer start.
size_t move_vim_word_backward_n(const std::string& buffer,
                                size_t pos,
                                size_t count,
                                bool big_word);

/// Deletes count codepoints under the current cursor in Vim normal mode (`x`).
/// MUST clamp at end-of-buffer and keep cursor at the original byte position.
/// Inputs are mutable buffer/cursor and a count; returns true when text changed.
bool delete_vim_chars_under_cursor(std::string& buffer, size_t& cursor, size_t count);

/// Deletes text for Vim delete-motion commands (`dw`, `db`, `dW`, `dB`, `d$`).
/// `motion` must be one of `w/W/b/B/$`; unknown motions are no-ops.
/// `line_end` is the current logical line end for `d$`.
bool delete_vim_motion(std::string& buffer,
                       size_t& cursor,
                       size_t count,
                       char motion,
                       size_t line_end);

}  // namespace xsql::cli
