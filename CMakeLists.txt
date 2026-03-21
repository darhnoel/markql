cmake_minimum_required(VERSION 3.16)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/python/markql/_meta.py" MARKQL_META_PY)
string(REGEX MATCH "__version__ = \"([0-9]+\\.[0-9]+\\.[0-9]+)\"" MARKQL_VERSION_MATCH "${MARKQL_META_PY}")
if (MARKQL_VERSION_MATCH)
  set(MARKQL_PROJECT_VERSION "${CMAKE_MATCH_1}")
else()
  set(MARKQL_PROJECT_VERSION "0.0.0")
endif()
project(markql VERSION ${MARKQL_PROJECT_VERSION} LANGUAGES CXX)

set(_markql_build_agent_default ON)
if (DEFINED XSQL_BUILD_AGENT)
  set(_markql_build_agent_default "${XSQL_BUILD_AGENT}")
endif()
set(_markql_agent_fetch_deps_default ON)
if (DEFINED XSQL_AGENT_FETCH_DEPS)
  set(_markql_agent_fetch_deps_default "${XSQL_AGENT_FETCH_DEPS}")
endif()

option(MARKQL_BUILD_CLI "Build the MarkQL CLI" ON)
option(MARKQL_BUILD_TESTS "Build tests" ON)
option(MARKQL_BUILD_PYTHON "Build the Python module" OFF)
option(MARKQL_BUILD_AGENT "Build the localhost xsql-agent service" ${_markql_build_agent_default})
option(MARKQL_WITH_LIBXML2 "Use libxml2 for HTML parsing" ON)
option(MARKQL_WITH_NLOHMANN_JSON "Use nlohmann/json for CLI output" ON)
option(MARKQL_WITH_CURL "Use libcurl for URL fetching" ON)
option(MARKQL_WITH_ARROW "Enable Apache Arrow for Parquet export" ON)
option(MARKQL_WITH_FLATBUFFERS "Use FlatBuffers for DOCN artifact payloads" ON)
option(MARKQL_ENABLE_KHMER_NUMBER "Enable built-in Khmer number module" ON)
option(MARKQL_AGENT_FETCH_DEPS "Fetch xsql-agent deps with FetchContent when missing" ${_markql_agent_fetch_deps_default})
option(MARKQL_OPTIMIZE_FOR_SIZE "Enable size-focused compile/link flags" OFF)
option(MARKQL_STRIP_BINARIES "Strip release-like binaries after build" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MARKQL_OPTIMIZE_FOR_SIZE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-ffunction-sections -fdata-sections)
  add_link_options(-Wl,--gc-sections)
endif()

if (MARKQL_OPTIMIZE_FOR_SIZE)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT MARKQL_IPO_SUPPORTED OUTPUT MARKQL_IPO_OUTPUT)
  if (MARKQL_IPO_SUPPORTED)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL ON)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
  else()
    message(STATUS "IPO/LTO not enabled: ${MARKQL_IPO_OUTPUT}")
  endif()
endif()

function(markql_maybe_strip_target target_name)
  if (MARKQL_STRIP_BINARIES AND CMAKE_STRIP AND UNIX)
    add_custom_command(
      TARGET ${target_name}
      POST_BUILD
      COMMAND ${CMAKE_STRIP} "$<TARGET_FILE:${target_name}>"
      COMMENT "Stripping ${target_name}"
      VERBATIM
    )
  endif()
endfunction()

add_library(markql_core
  core/src/version.cpp
  core/src/artifacts/artifacts.cpp
  core/src/artifacts/artifact_internal.cpp
  core/src/artifacts/artifact_document.cpp
  core/src/artifacts/artifact_query.cpp
  core/src/artifacts/artifact_query_flatbuffers.cpp
  core/src/lang/ast.cpp
  core/src/lang/diagnostics.cpp
  core/src/lang/markql_parser.cpp
  core/src/lang/parser/parser.cpp
  core/src/lang/parser/parser_expr.cpp
  core/src/lang/parser/parser_query.cpp
  core/src/lang/parser/parser_select.cpp
  core/src/lang/parser/parser_source.cpp
  core/src/lang/parser/parser_util.cpp
  core/src/lang/parser/lexer.cpp
  core/src/dom/html_parser.cpp
  core/src/dom/backend/parser_naive.cpp
  core/src/dom/backend/parser_libxml2.cpp
  core/src/runtime/executor/executor.cpp
  core/src/runtime/executor/filter.cpp
  core/src/runtime/executor/order.cpp
  core/src/util/string_util.cpp
  core/src/runtime/engine/execute.cpp
  core/src/runtime/engine/execute_common.cpp
  core/src/runtime/engine/execute_dom_descendants.cpp
  core/src/runtime/engine/execute_dom_projection.cpp
  core/src/runtime/engine/execute_dom.cpp
  core/src/runtime/engine/execute_fragments.cpp
  core/src/runtime/engine/execute_lint.cpp
  core/src/runtime/engine/execute_meta.cpp
  core/src/runtime/engine/execute_output.cpp
  core/src/runtime/engine/execute_relation_join.cpp
  core/src/runtime/engine/execute_relation_expr.cpp
  core/src/runtime/engine/execute_relation_result.cpp
  core/src/runtime/engine/execute_relation.cpp
  core/src/runtime/engine/execute_source.cpp
  core/src/runtime/engine/query_validation_entry.cpp
  core/src/runtime/engine/io.cpp
  core/src/runtime/engine/column_names.cpp
  core/src/runtime/engine/query_validation_rules.cpp
  core/src/runtime/engine/result_builder.cpp
  core/src/runtime/engine/table_extract.cpp
  core/src/runtime/engine/tfidf.cpp
)

target_include_directories(markql_core
  PUBLIC
    core/include
  PRIVATE
    core/src
)

target_compile_features(markql_core PUBLIC cxx_std_20)
execute_process(
  COMMAND git rev-parse --short=12 HEAD
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE MARKQL_GIT_COMMIT
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
if (NOT MARKQL_GIT_COMMIT)
  set(MARKQL_GIT_COMMIT "unknown")
endif()

execute_process(
  COMMAND git diff --quiet --ignore-submodules HEAD --
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  RESULT_VARIABLE MARKQL_GIT_DIRTY_RESULT
  ERROR_QUIET
)
set(MARKQL_GIT_DIRTY 0)
if (NOT MARKQL_GIT_DIRTY_RESULT EQUAL 0)
  set(MARKQL_GIT_DIRTY 1)
endif()

target_compile_definitions(markql_core
  PUBLIC
    MARKQL_VERSION="${PROJECT_VERSION}"
    MARKQL_GIT_COMMIT="${MARKQL_GIT_COMMIT}"
    MARKQL_GIT_DIRTY=${MARKQL_GIT_DIRTY}
)

if (MARKQL_WITH_FLATBUFFERS)
  find_package(flatbuffers CONFIG QUIET)
  if (NOT flatbuffers_FOUND)
    message(FATAL_ERROR "flatbuffers not found. Configure with the repo vcpkg manifest/build path or set MARKQL_WITH_FLATBUFFERS=OFF.")
  endif()
  set(MARKQL_DOCN_SCHEMA "${CMAKE_CURRENT_SOURCE_DIR}/core/src/artifacts/schema/document.fbs")
  set(MARKQL_QAST_SCHEMA "${CMAKE_CURRENT_SOURCE_DIR}/core/src/artifacts/schema/prepared_query_payload.fbs")
  flatbuffers_generate_headers(
    TARGET markql_artifact_docn_flatbuffers
    SCHEMAS ${MARKQL_DOCN_SCHEMA}
    INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/core/src/artifacts/schema"
  )
  flatbuffers_generate_headers(
    TARGET markql_artifact_qast_flatbuffers
    SCHEMAS ${MARKQL_QAST_SCHEMA}
    INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/core/src/artifacts/schema"
  )
  add_dependencies(markql_core GENERATE_markql_artifact_docn_flatbuffers)
  add_dependencies(markql_core GENERATE_markql_artifact_qast_flatbuffers)
  target_link_libraries(markql_core PRIVATE flatbuffers::flatbuffers
                                          markql_artifact_docn_flatbuffers
                                          markql_artifact_qast_flatbuffers)
endif()

if (MARKQL_ENABLE_KHMER_NUMBER)
  target_sources(markql_core PRIVATE core/src/khmer_number.cpp)
  target_compile_definitions(markql_core PUBLIC MARKQL_ENABLE_KHMER_NUMBER)
endif()

if (MARKQL_WITH_LIBXML2)
  find_package(LibXml2)
  if (LibXml2_FOUND)
    target_include_directories(markql_core PRIVATE ${LIBXML2_INCLUDE_DIR})
    target_link_libraries(markql_core PRIVATE ${LIBXML2_LIBRARIES})
    target_compile_definitions(markql_core PRIVATE MARKQL_USE_LIBXML2)
  else()
    message(WARNING "LibXml2 not found; falling back to naive HTML parser.")
  endif()
endif()

if (MARKQL_WITH_CURL)
  find_package(CURL CONFIG QUIET)
  if (TARGET CURL::libcurl)
    target_link_libraries(markql_core PRIVATE CURL::libcurl)
    target_compile_definitions(markql_core PUBLIC MARKQL_USE_CURL)
  else()
    find_package(CURL)
    if (CURL_FOUND)
      target_include_directories(markql_core PRIVATE ${CURL_INCLUDE_DIRS})
      target_link_libraries(markql_core PRIVATE ${CURL_LIBRARIES})
      target_compile_definitions(markql_core PUBLIC MARKQL_USE_CURL)
    else()
      message(WARNING "libcurl not found; URL fetching disabled.")
    endif()
  endif()
endif()

if (MARKQL_WITH_ARROW)
  find_package(Arrow CONFIG QUIET)
  find_package(Parquet CONFIG QUIET)
  if (TARGET Arrow::arrow_shared)
    set(MARKQL_ARROW_TARGET Arrow::arrow_shared)
  elseif (TARGET Arrow::arrow_static)
    set(MARKQL_ARROW_TARGET Arrow::arrow_static)
  elseif (TARGET Arrow::arrow)
    set(MARKQL_ARROW_TARGET Arrow::arrow)
  elseif (TARGET arrow_shared)
    set(MARKQL_ARROW_TARGET arrow_shared)
  elseif (TARGET arrow_static)
    set(MARKQL_ARROW_TARGET arrow_static)
  elseif (TARGET arrow)
    set(MARKQL_ARROW_TARGET arrow)
  endif()
  if (TARGET Parquet::parquet_shared)
    set(MARKQL_PARQUET_TARGET Parquet::parquet_shared)
  elseif (TARGET Parquet::parquet_static)
    set(MARKQL_PARQUET_TARGET Parquet::parquet_static)
  elseif (TARGET Parquet::parquet)
    set(MARKQL_PARQUET_TARGET Parquet::parquet)
  elseif (TARGET parquet_shared)
    set(MARKQL_PARQUET_TARGET parquet_shared)
  elseif (TARGET parquet_static)
    set(MARKQL_PARQUET_TARGET parquet_static)
  elseif (TARGET parquet)
    set(MARKQL_PARQUET_TARGET parquet)
  endif()
  if (NOT MARKQL_ARROW_TARGET OR NOT MARKQL_PARQUET_TARGET)
    message(WARNING "Arrow/Parquet not found; TO PARQUET disabled.")
  endif()
endif()

if (MARKQL_BUILD_AGENT)
  add_subdirectory(browser_plugin/agent)
endif()

if (MARKQL_BUILD_CLI)
  add_executable(markql
    cli/main.cpp
    cli/cli_args.cpp
    cli/cli_utils.cpp
    cli/render/query_template_renderer.cpp
    cli/explore/dom_explorer.cpp
    cli/explore/inner_html_search.cpp
    cli/explore/markql_suggestor.cpp
    cli/script_runner.cpp
    cli/repl/core/vim_edit.cpp
    cli/repl/core/vim_normal.cpp
    cli/repl/core/line_editor.cpp
    cli/repl/ui/autocomplete.cpp
    cli/repl/ui/sql_keywords.cpp
    cli/repl/state/history.cpp
    cli/repl/input/terminal.cpp
    cli/repl/ui/render.cpp
    cli/repl/input/text_util.cpp
    cli/repl/core/repl.cpp
    cli/repl/config.cpp
    cli/repl/commands/registry.cpp
    cli/repl/commands/help_command.cpp
    cli/repl/commands/set_command.cpp
    cli/repl/commands/lint_command.cpp
    cli/repl/commands/describe_last_command.cpp
    cli/repl/commands/display_mode_command.cpp
    cli/repl/commands/mode_command.cpp
    cli/repl/commands/max_rows_command.cpp
    cli/repl/commands/reload_config_command.cpp
    cli/repl/commands/explore_command.cpp
    cli/repl/commands/summarize_command.cpp
    cli/repl/commands/load_command.cpp
    cli/repl/commands/plugin_command.cpp
    cli/repl/commands/plugin_install_command.cpp
    cli/repl/commands/plugin_registry.cpp
    cli/repl/commands/summarize_content_command.cpp
    cli/repl/plugin_manager.cpp
    cli/ui/color.cpp
    cli/export/export_sinks.cpp
    cli/render/duckbox_renderer.cpp
  )
  if (MARKQL_ENABLE_KHMER_NUMBER)
    target_sources(markql PRIVATE cli/repl/commands/khmer_number_command.cpp)
  endif()
  target_link_libraries(markql PRIVATE markql_core)
  target_include_directories(markql PRIVATE core/src core/include cli)
  target_compile_definitions(markql PRIVATE MARKQL_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
  if (UNIX AND NOT APPLE)
    target_link_libraries(markql PRIVATE dl)
  endif()
  if (MARKQL_WITH_NLOHMANN_JSON)
    find_package(nlohmann_json CONFIG)
    if (nlohmann_json_FOUND)
      target_link_libraries(markql PRIVATE nlohmann_json::nlohmann_json)
      target_compile_definitions(markql PRIVATE MARKQL_USE_NLOHMANN_JSON)
    else()
      message(WARNING "nlohmann_json not found; using built-in JSON output.")
    endif()
  endif()
  if (MARKQL_ARROW_TARGET AND MARKQL_PARQUET_TARGET)
    target_link_libraries(markql PRIVATE ${MARKQL_ARROW_TARGET} ${MARKQL_PARQUET_TARGET})
    target_compile_definitions(markql PRIVATE MARKQL_USE_ARROW)
  endif()
  set_target_properties(markql PROPERTIES OUTPUT_NAME markql)
  add_custom_command(
    TARGET markql
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:markql>"
            "$<TARGET_FILE_DIR:markql>/markql${CMAKE_EXECUTABLE_SUFFIX}"
    COMMENT "Creating compatibility binary: markql -> markql"
  )
  markql_maybe_strip_target(markql)
endif()

if (MARKQL_BUILD_TESTS)
  enable_testing()
  add_executable(markql_tests
    tests/test_main.cpp
    tests/test_harness.cpp
    tests/test_utils.cpp
    tests/test_query_basic.cpp
    tests/test_sources_alias.cpp
    tests/test_with_join.cpp
    tests/test_shorthand.cpp
    tests/test_projections.cpp
    tests/test_functions.cpp
    tests/test_axes.cpp
    tests/test_predicates.cpp
    tests/test_order_by.cpp
    tests/test_duckbox.cpp
    tests/test_exports.cpp
    tests/test_artifacts.cpp
    tests/test_malformed_html.cpp
    tests/test_flatten_text.cpp
    tests/test_flatten_extract.cpp
    tests/test_repl.cpp
    tests/test_fragments.cpp
    tests/test_guardrails.cpp
    tests/test_meta_commands.cpp
    tests/test_cli_utils.cpp
    tests/test_lexer_comments.cpp
    tests/test_script_runner.cpp
    tests/test_cli_args.cpp
    tests/test_string_sql.cpp
    tests/test_column_names.cpp
    tests/test_self_ref.cpp
    tests/test_query_template_renderer.cpp
    tests/test_vim_edit.cpp
    tests/test_vim_normal.cpp
    tests/test_render.cpp
    tests/test_dom_explorer.cpp
    cli/explore/dom_explorer.cpp
    cli/explore/inner_html_search.cpp
    cli/explore/markql_suggestor.cpp
    tests/test_explore_search.cpp
    tests/test_markql_suggestor.cpp
    tests/test_table_options.cpp
    tests/test_diagnostics.cpp
    cli/repl/core/vim_edit.cpp
    cli/repl/core/vim_normal.cpp
    cli/repl/core/line_editor.cpp
    cli/repl/ui/autocomplete.cpp
    cli/repl/ui/sql_keywords.cpp
    cli/repl/state/history.cpp
    cli/repl/input/terminal.cpp
    cli/repl/ui/render.cpp
    cli/repl/input/text_util.cpp
    cli/repl/plugin_manager.cpp
    cli/repl/config.cpp
    cli/repl/commands/registry.cpp
    cli/repl/commands/help_command.cpp
    cli/repl/commands/set_command.cpp
    cli/repl/commands/lint_command.cpp
    cli/repl/commands/describe_last_command.cpp
    cli/repl/commands/display_mode_command.cpp
    cli/repl/commands/mode_command.cpp
    cli/repl/commands/max_rows_command.cpp
    cli/repl/commands/reload_config_command.cpp
    cli/repl/commands/explore_command.cpp
    cli/repl/commands/summarize_command.cpp
    cli/repl/commands/load_command.cpp
    cli/repl/commands/plugin_command.cpp
    cli/repl/commands/plugin_install_command.cpp
    cli/repl/commands/plugin_registry.cpp
    cli/repl/commands/summarize_content_command.cpp
    cli/cli_args.cpp
    cli/cli_utils.cpp
    cli/render/query_template_renderer.cpp
    cli/script_runner.cpp
    cli/ui/color.cpp
    cli/export/export_sinks.cpp
    cli/render/duckbox_renderer.cpp
  )
  if (MARKQL_ENABLE_KHMER_NUMBER)
    target_sources(markql_tests PRIVATE
      cli/repl/commands/khmer_number_command.cpp
      tests/test_khmer_number.cpp
    )
  endif()
  target_link_libraries(markql_tests PRIVATE markql_core)
  target_include_directories(markql_tests PRIVATE cli core/src core/include tests)
  target_compile_definitions(markql_tests PRIVATE MARKQL_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
  if (MARKQL_WITH_FLATBUFFERS)
    target_link_libraries(markql_tests PRIVATE flatbuffers::flatbuffers
                                             markql_artifact_docn_flatbuffers
                                             markql_artifact_qast_flatbuffers)
  endif()
  if (MARKQL_ARROW_TARGET AND MARKQL_PARQUET_TARGET)
    target_link_libraries(markql_tests PRIVATE ${MARKQL_ARROW_TARGET} ${MARKQL_PARQUET_TARGET})
    target_compile_definitions(markql_tests PRIVATE MARKQL_USE_ARROW)
  endif()
  markql_maybe_strip_target(markql_tests)
  add_executable(markql_bench_inner_html tests/bench_inner_html.cpp)
  target_link_libraries(markql_bench_inner_html PRIVATE markql_core)
  target_include_directories(markql_bench_inner_html PRIVATE core/include)
  if (MARKQL_ARROW_TARGET AND MARKQL_PARQUET_TARGET)
    target_link_libraries(markql_bench_inner_html PRIVATE ${MARKQL_ARROW_TARGET} ${MARKQL_PARQUET_TARGET})
    target_compile_definitions(markql_bench_inner_html PRIVATE MARKQL_USE_ARROW)
  endif()
  set_target_properties(markql_bench_inner_html PROPERTIES OUTPUT_NAME markql_bench_inner_html)
  add_custom_command(
    TARGET markql_bench_inner_html
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:markql_bench_inner_html>"
            "$<TARGET_FILE_DIR:markql_bench_inner_html>/markql_bench_inner_html${CMAKE_EXECUTABLE_SUFFIX}"
    COMMENT "Creating compatibility binary: markql_bench_inner_html -> markql_bench_inner_html"
  )
  markql_maybe_strip_target(markql_bench_inner_html)
  add_executable(markql_bench_artifacts tests/bench_artifacts.cpp)
  target_link_libraries(markql_bench_artifacts PRIVATE markql_core)
  target_include_directories(markql_bench_artifacts PRIVATE cli core/src core/include tests)
  if (MARKQL_WITH_FLATBUFFERS)
    target_link_libraries(markql_bench_artifacts PRIVATE flatbuffers::flatbuffers
                                                      markql_artifact_docn_flatbuffers
                                                      markql_artifact_qast_flatbuffers)
  endif()
  set_target_properties(markql_bench_artifacts PROPERTIES OUTPUT_NAME markql_bench_artifacts)
  add_custom_command(
    TARGET markql_bench_artifacts
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:markql_bench_artifacts>"
            "$<TARGET_FILE_DIR:markql_bench_artifacts>/markql_bench_artifacts${CMAKE_EXECUTABLE_SUFFIX}"
    COMMENT "Creating compatibility binary: markql_bench_artifacts -> markql_bench_artifacts"
  )
  markql_maybe_strip_target(markql_bench_artifacts)
  set(MARKQL_TESTS
    select_ul_by_id
    class_in_matches_token
    parent_attribute_filter
    multi_tag_select
    select_star
    class_eq_matches_token
    missing_attribute_no_match
    invalid_query_throws
    text_requires_non_tag_filter
    projection_parent_id
    projection_attributes
    projection_tag_field_list
    projection_sibling_pos
    inner_html_function
    minify_html_basic
    minify_html_preserves_attribute_quotes
    minify_html_preserves_protected_tags
    minify_html_preserves_script_style
    inner_html_depth
    trim_inner_html
    trim_mixed_with_other_projection
    inner_html_minified_by_default
    raw_inner_html_opt_out
    child_axis_direct_only
    ancestor_filter_on_a
    limit
    alias_qualifier
    alias_source_only
    attr_shorthand_self_and_qualified_aliases
    attr_shorthand_axis_paths
    parse_alias_field_with_implicit_doc
    parse_alias_field_with_explicit_alias
    parse_legacy_tag_binding_still_works
    duplicate_source_alias_rejected
    implicit_doc_alias_matches_explicit_alias_values
    doc_identifier_rejected_after_explicit_realias
    unknown_identifier_error_mentions_alias_or_legacy_binding
    parse_with_single_and_multiple_ctes
    parse_derived_table_and_joins
    parse_cross_join_lateral
    parse_reject_duplicate_cte_name
    parse_reject_derived_table_without_alias
    parse_reject_join_without_on
    parse_reject_cross_join_with_on
    parse_reject_lateral_without_alias
    lateral_unknown_alias_throws
    with_left_join_lateral_baseline_values
    with_left_join_lateral_missing_right_value_null
    lateral_select_self_equivalent_to_select_alias
    with_qualified_parent_axis_and_case_projection
    shorthand_attribute_filter
    shorthand_qualified_attribute_filter
    ancestor_attribute_filter
    descendant_attribute_filter
    parent_tag_filter
    parent_id_filter
    attributes_is_null
    attributes_is_not_null
    node_id_filter
    select_exclude_single
    select_exclude_list
    to_table_flag
    to_list_flag
    attribute_projection_value
    count_aggregate
    count_star
    summarize_star
    summarize_limit
    summarize_order_by_count
    order_by_tag
    order_by_node_id_desc
    order_by_multi
    not_equal_attribute
    is_not_null_attribute
    is_null_attribute
    text_not_equal
    regex_attribute
    contains_attribute
    contains_all_attribute
    contains_any_attribute
    sibling_pos_filter
    has_direct_text
    parenthesized_predicates
    exists_child_any
    exists_child_tag
    exists_child_same_node
    duckbox_basic_table
    duckbox_truncate_cells
    duckbox_maxrows_truncate
    duckbox_numeric_alignment
    duckbox_null_rendering
    csv_escaping
    csv_export_integration
    json_export_integration
    ndjson_export_integration
    json_ndjson_stdout_fallback
    artifact_cli_args
    artifact_html_and_mqd_results_match
    artifact_query_and_mqp_results_match
    artifact_mqp_on_mqd_matches_direct
    artifact_lint_behavior_unchanged
    artifact_document_round_trip_is_deterministic
    artifact_query_round_trip_is_deterministic
    artifact_document_major_version_rejected
    artifact_query_major_version_rejected
    artifact_corrupted_files_fail_safely
    artifact_inspect_reports_metadata
    raw_source_literal
    fragments_from_raw
    fragments_from_inner_html
    fragments_non_html_error
    fragments_warn_deprecated
    parse_from_string_expr
    parse_from_subquery
    guardrails_limit_cap
    guardrails_regex_length_cap
    guardrails_fragments_count_cap
    malformed_missing_closing_tags
    malformed_mismatched_nesting
    malformed_junk_bytes_no_throw
    summarize_content_basic
    summarize_content_khmer_requires_plugin
    summarize_content_max_tokens
    set_colnames_command
    lint_command_toggles_on_and_off
    lint_command_rejects_invalid_value
    describe_last_command_outputs_map
    explore_command_uses_active_alias_by_default
    explore_command_accepts_direct_target_and_alias_target
    parse_show_describe
    execute_describe_doc
    show_functions_output
    describe_language_output
    show_input_requires_source
    show_inputs_requires_source
    show_inputs_fallback_source
    auto_include_source_uri
    count_table_rows_header
    count_table_rows_no_header
    count_result_rows
    proportional_column_end_maps_to_end
    proportional_column_scales_middle
    proportional_column_zero_source_len
    flatten_extract_basic
    flatten_extract_has_direct_text_predicate
    flatten_extract_requires_as_pairs
    flatten_extract_alias_compatibility
    flatten_extract_table_drift_stability
    parse_like_predicate
    parse_position_with_in
    parse_project_nested_string_functions
    parse_legacy_has_direct_text
    parse_scoped_selector_inside_text
    parse_case_expression_in_select
    parse_nested_case_expression
    parse_parse_source_forms
    eval_like_wildcards
    eval_string_functions_in_select
    eval_length_byte_semantics
    eval_position_found_and_not_found
    eval_direct_text_excludes_descendants
    eval_case_expression_select_and_null_else
    project_case_and_selector_positions
    project_missing_match_null_and_coalesce
    project_selector_scope_and_first_match
    project_top_level_comparison_expression
    regression_canonical_project_query
    normalize_colname_examples
    normalize_colname_collision_suffixing
    duckbox_uses_normalized_headers_by_default
    duckbox_raw_mode_keeps_raw_header
    csv_header_normalized_default
    csv_header_raw_mode
    json_keys_follow_colname_mode
    inspect_sql_input_line_comment_only
    inspect_sql_input_block_comment_only
    inspect_sql_input_unterminated_block_comment
    line_comment_before_tokens
    line_comment_between_tokens
    line_comment_at_eof
    block_comment_single_line
    block_comment_multi_line
    unterminated_block_comment_error
    comment_markers_inside_string_literals
    query_without_comments_unchanged_behavior
    split_script_ignores_empty_statements
    split_script_with_comments
    split_script_unterminated_block_comment
    split_script_semicolon_and_comment_markers_inside_string_literals
    run_script_multi_statement_delimiters
    run_script_comments_and_empty_statements
    run_script_stops_on_first_error_by_default
    run_script_continue_on_error
    run_script_quiet_suppresses_delimiter
    utf8_validation_for_script_file_content
    parse_cli_args_accepts_script_flags
    parse_cli_args_rejects_missing_value
    parse_cli_args_rejects_unknown_argument
    parse_cli_args_rejects_query_and_query_file_together
    parse_cli_args_accepts_lint_inline_query
    parse_cli_args_accepts_lint_json_format
    parse_cli_args_rejects_format_without_lint
    parse_cli_args_accepts_version_flag
    parse_cli_args_accepts_render_flags
    parse_cli_args_rejects_vars_without_render
    parse_cli_args_rejects_render_without_query_file
    parse_cli_args_rejects_unknown_render_mode
    parse_cli_args_rejects_render_stdout_with_lint
    parse_cli_args_accepts_color_modes
    parse_cli_args_rejects_invalid_color_mode
    diagnostics_color_policy_modes_and_no_color_precedence
    output_color_policy_auto_and_no_color
    lint_syntax_diagnostic_has_stable_code_and_span
    lint_semantic_diagnostic_has_stable_code
    lint_warns_select_from_alias_as_ambiguous_node_value
    lint_select_self_has_no_alias_ambiguity_warning
    diagnostic_text_renderer_contains_help_and_caret
    diagnostic_json_renderer_contains_stable_fields
    diagnostic_json_renderer_matches_snapshot
    diagnostic_text_renderer_matches_golden_snippet
    diagnostic_text_renderer_color_includes_ansi_tokens
    diagnostic_json_renderer_never_contains_ansi_sequences
    diagnose_query_failure_maps_parse_error
    version_string_contains_provenance
    query_template_renderer_matches_golden_output
    query_template_renderer_writes_exact_output_file
    query_template_renderer_requires_defined_variables
    query_template_renderer_rejects_invalid_toml
    query_template_renderer_rejects_invalid_template_syntax
    parse_self_projection_and_where
    parse_select_self_node_projection
    parse_self_function_predicate
    parse_self_text_attr_inner_html_functions
    eval_direct_text_self_without_tag_guessing
    eval_select_self_returns_current_row_node
    eval_self_rebind_inside_exists_descendant
    eval_text_and_attr_with_self
    eval_inner_html_with_self
    eval_raw_inner_html_with_self
    select_mixing_tag_and_self_projection_still_errors
    flatten_visible_tree_order_and_depth
    render_attribute_lines_sorted_format
    fuzzy_match_score_basic
    fuzzy_search_orders_and_snippet
    fuzzy_search_candidate_subset
    fuzzy_search_prioritizes_word_level_match
    make_inner_html_snippet_context
    table_header_normalize_behavior
    table_empty_is_semantics
    table_trim_empty_rows_semantics
    table_trim_empty_cols_semantics
    table_sparse_non_empty_cells_only
    golden_table_trim_baseline
    golden_table_trim_rows
    golden_table_trim_trailing_cols
    golden_table_trim_rows_and_trailing_cols
    golden_table_trim_all_cols
    golden_table_stop_after_empty_rows
    golden_table_header_normalize
    golden_table_sparse_long
    golden_table_sparse_wide
  )
  if (MARKQL_ENABLE_KHMER_NUMBER)
    list(APPEND MARKQL_TESTS
      khmer_number_digits
      khmer_number_scale_boundaries
      khmer_number_decimals_negatives
      khmer_number_concatenated_input
      khmer_number_round_trip_random
      khmer_number_golden_vectors
      khmer_number_command_to_words
      khmer_number_command_compact
    )
  endif()
  if (MARKQL_ARROW_TARGET AND MARKQL_PARQUET_TARGET)
    list(APPEND MARKQL_TESTS parquet_export_smoke)
  endif()
  foreach(test_name IN LISTS MARKQL_TESTS)
    add_test(NAME markql_${test_name} COMMAND markql_tests ${test_name})
  endforeach()
endif()

if (MARKQL_BUILD_PYTHON)
  find_package(Python COMPONENTS Development.Module REQUIRED)
  find_package(pybind11 CONFIG REQUIRED)
  pybind11_add_module(_core python/markql/_core.cpp)
  target_link_libraries(_core PRIVATE markql_core)
  target_include_directories(_core PRIVATE core/include)
  set_target_properties(_core PROPERTIES OUTPUT_NAME "_core")
  markql_maybe_strip_target(_core)
  if (DEFINED SKBUILD_PLATLIB_DIR)
    set_target_properties(_core PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${SKBUILD_PLATLIB_DIR}/markql")
  endif()
endif()

# Local packaging support (e.g., cpack -G DEB on Ubuntu).
if (MARKQL_BUILD_CLI)
  install(TARGETS markql RUNTIME DESTINATION bin)
  if (EXISTS "${CMAKE_SOURCE_DIR}/scripts/cli/markqli.sh")
    install(PROGRAMS "${CMAKE_SOURCE_DIR}/scripts/cli/markqli.sh" DESTINATION bin)
  endif()
endif()
if (EXISTS "${CMAKE_SOURCE_DIR}/README.md")
  install(FILES "${CMAKE_SOURCE_DIR}/README.md" DESTINATION share/doc/markql)
endif()
if (EXISTS "${CMAKE_SOURCE_DIR}/LICENSE")
  install(FILES "${CMAKE_SOURCE_DIR}/LICENSE" DESTINATION share/doc/markql)
endif()
if (EXISTS "${CMAKE_SOURCE_DIR}/docs/man/markql.1")
  install(FILES "${CMAKE_SOURCE_DIR}/docs/man/markql.1" DESTINATION share/man/man1)
endif()
if (EXISTS "${CMAKE_SOURCE_DIR}/completions/markql.bash")
  install(FILES "${CMAKE_SOURCE_DIR}/completions/markql.bash"
          DESTINATION share/bash-completion/completions
          RENAME markql)
  install(FILES "${CMAKE_SOURCE_DIR}/completions/markql.bash"
          DESTINATION share/bash-completion/completions
          RENAME markql)
endif()

set(CPACK_PACKAGE_NAME "markql")
set(CPACK_PACKAGE_VENDOR "MarkQL")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_CONTACT "maintainers@markql.local")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "MarkQL CLI for querying HTML with SQL-like syntax")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

include(CPack)
