#include "describe_last_command.h"

#include <algorithm>
#include <cctype>
#include <iostream>

#include "../../cli_utils.h"
#include "../../render/duckbox_renderer.h"

namespace xsql::cli {

namespace {

std::string to_lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

}  // namespace

CommandHandler make_describe_last_command() {
  return [](const std::string& line, CommandContext& ctx) -> bool {
    std::string lowered = to_lower(trim_semicolon(line));
    if (lowered != "describe last") {
      return false;
    }
    xsql::QueryResult result;
    result.columns = {"raw_name", "output_name"};
    for (const auto& item : ctx.last_schema_map) {
      xsql::QueryResultRow row;
      row.attributes["raw_name"] = item.raw_name;
      row.attributes["output_name"] = item.output_name;
      result.rows.push_back(std::move(row));
    }
    if (ctx.config.output_mode == "duckbox") {
      xsql::render::DuckboxOptions options;
      options.max_width = 0;
      options.max_rows = ctx.max_rows;
      options.highlight = ctx.config.highlight;
      options.is_tty = ctx.config.color;
      options.colname_mode = ctx.config.colname_mode;
      std::cout << xsql::render::render_duckbox(result, options) << std::endl;
      std::cout << "Rows: " << count_result_rows(result) << std::endl;
    } else {
      std::string json_out = build_json(result, ctx.config.colname_mode);
      ctx.last_full_output = json_out;
      if (ctx.config.output_mode == "plain") {
        std::cout << json_out << std::endl;
      } else if (ctx.display_full) {
        std::cout << colorize_json(json_out, ctx.config.color) << std::endl;
      } else {
        TruncateResult truncated = truncate_output(json_out, 10, 10);
        std::cout << colorize_json(truncated.output, ctx.config.color) << std::endl;
      }
      std::cout << "Rows: " << count_result_rows(result) << std::endl;
    }
    return true;
  };
}

}  // namespace xsql::cli
