#pragma once

#include <memory>
#include <optional>
#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace xsql {

struct Span {
  size_t start = 0;
  size_t end = 0;
};

struct Query;
struct ScalarExpr;

struct Source {
  enum class Kind {
    Document,
    Path,
    Url,
    RawHtml,
    Fragments,
    Parse,
    CteRef,
    DerivedSubquery
  } kind = Kind::Document;
  std::string value;
  std::optional<std::string> alias;
  std::shared_ptr<Query> fragments_query;
  std::optional<std::string> fragments_raw;
  std::shared_ptr<Query> parse_query;
  std::shared_ptr<ScalarExpr> parse_expr;
  std::shared_ptr<Query> derived_query;
  Span span;
};

struct Operand {
  enum class Axis { Self, Parent, Child, Ancestor, Descendant } axis = Axis::Self;
  enum class FieldKind {
    Attribute,
    AttributesMap,
    Tag,
    Text,
    NodeId,
    ParentId,
    SiblingPos,
    MaxDepth,
    DocOrder
  } field_kind = FieldKind::Attribute;
  std::string attribute;
  std::optional<std::string> qualifier;
  Span span;
};

struct SelfRef {
  Span span;
};

struct ScalarExpr {
  enum class Kind {
    Operand,
    SelfRef,
    StringLiteral,
    NumberLiteral,
    NullLiteral,
    FunctionCall
  } kind = Kind::Operand;
  Operand operand;
  SelfRef self_ref;
  std::string string_value;
  int64_t number_value = 0;
  std::string function_name;
  std::vector<ScalarExpr> args;
  Span span;
};

struct ValueList {
  std::vector<std::string> values;
  Span span;
};

struct CompareExpr {
  enum class Op {
    Eq,
    In,
    NotEq,
    Lt,
    Lte,
    Gt,
    Gte,
    IsNull,
    IsNotNull,
    Regex,
    Like,
    Contains,
    ContainsAll,
    ContainsAny,
    HasDirectText
  } op = Op::Eq;
  Operand lhs;
  ValueList rhs;
  std::optional<ScalarExpr> lhs_expr;
  std::optional<ScalarExpr> rhs_expr;
  std::vector<ScalarExpr> rhs_expr_list;
  Span span;
};

struct ExistsExpr;
struct BinaryExpr;
using Expr = std::variant<CompareExpr, std::shared_ptr<ExistsExpr>, std::shared_ptr<BinaryExpr>>;

struct ExistsExpr {
  Operand::Axis axis = Operand::Axis::Self;
  std::optional<Expr> where;
  Span span;
};

struct BinaryExpr {
  enum class Op { And, Or } op = Op::And;
  Expr left;
  Expr right;
  Span span;
};

struct Query {
  enum class Kind {
    Select,
    ShowInput,
    ShowInputs,
    ShowFunctions,
    ShowAxes,
    ShowOperators,
    DescribeDoc,
    DescribeLanguage
  } kind = Kind::Select;
  struct WithClause {
    struct CteDef {
      std::string name;
      std::shared_ptr<Query> query;
      Span span;
    };
    std::vector<CteDef> ctes;
    Span span;
  };
  struct JoinItem {
    enum class Type { Inner, Left, Cross } type = Type::Inner;
    Source right_source;
    std::optional<Expr> on;
    bool lateral = false;
    Span span;
  };
  struct ExportSink {
    enum class Kind { None, Csv, Parquet, Json, Ndjson } kind = Kind::None;
    std::string path;
    Span span;
  };
  struct OrderBy {
    std::string field;
    bool descending = false;
    Span span;
  };
  struct TableOptions {
    enum class TrimEmptyCols { Off, Trailing, All } trim_empty_cols = TrimEmptyCols::Off;
    enum class EmptyIs { BlankOrNull, NullOnly, BlankOnly } empty_is = EmptyIs::BlankOrNull;
    enum class Format { Rect, Sparse } format = Format::Rect;
    enum class SparseShape { Long, Wide } sparse_shape = SparseShape::Long;
    bool trim_empty_rows = false;
    size_t stop_after_empty_rows = 0;
    bool header_normalize = true;
    bool header_normalize_explicit = false;
  };
  struct SelectItem {
    struct FlattenExtractExpr {
      enum class Kind {
        Text,
        Attr,
        Coalesce,
        FunctionCall,
        StringLiteral,
        NumberLiteral,
        NullLiteral,
        AliasRef,
        OperandRef,
        CaseWhen
      } kind = Kind::Text;
      std::string tag;
      std::optional<std::string> attribute;
      std::optional<Expr> where;
      std::optional<int64_t> selector_index;
      bool selector_last = false;
      std::vector<FlattenExtractExpr> args;
      std::string function_name;
      std::string string_value;
      int64_t number_value = 0;
      std::string alias_ref;
      Operand operand;
      std::vector<Expr> case_when_conditions;
      std::vector<FlattenExtractExpr> case_when_values;
      std::shared_ptr<FlattenExtractExpr> case_else;
      Span span;
    };
    enum class Aggregate { None, Count, Summarize, Tfidf } aggregate = Aggregate::None;
    enum class TfidfStopwords { English, None } tfidf_stopwords = TfidfStopwords::English;
    std::string tag;
    std::vector<std::string> tfidf_tags;
    std::optional<std::string> field;
    bool tfidf_all_tags = false;
    size_t tfidf_top_terms = 30;
    size_t tfidf_min_df = 1;
    size_t tfidf_max_df = 0;
    std::optional<size_t> inner_html_depth;
    bool inner_html_auto_depth = false;
    bool inner_html_function = false;
    bool raw_inner_html_function = false;
    bool text_function = false;
    bool trim = false;
    bool flatten_text = false;
    bool flatten_extract = false;
    bool expr_projection = false;
    std::optional<size_t> flatten_depth;
    std::vector<std::string> flatten_aliases;
    std::vector<std::string> flatten_extract_aliases;
    std::vector<FlattenExtractExpr> flatten_extract_exprs;
    std::optional<ScalarExpr> expr;
    std::optional<FlattenExtractExpr> project_expr;
    Span span;
  };
  std::optional<WithClause> with;
  std::vector<SelectItem> select_items;
  Source source;
  std::vector<JoinItem> joins;
  std::optional<Expr> where;
  std::vector<OrderBy> order_by;
  std::vector<std::string> exclude_fields;
  std::optional<size_t> limit;
  bool to_list = false;
  bool to_table = false;
  bool table_has_header = true;
  TableOptions table_options;
  std::optional<ExportSink> export_sink;
  Span span;
};

}  // namespace xsql
