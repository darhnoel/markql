# MarkQL

MarkQL is a SQL-style query engine for HTML: it turns a page into a table of nodes and lets you select, filter, and extract structured fields with `SELECT ... FROM ... WHERE ...`. This context defines the vocabulary used across the C++ engine, CLI, Python package, browser agent, and helper subsystem.

## Language

### Core semantics

**Two-stage evaluation**:
A query is a two-stage computation over a DOM row stream. Stage 1 decides which rows exist; Stage 2 computes each field's value for the surviving rows. This separation is the semantic center of the language.

**Row survival**:
Stage 1: whether a row stays in the output. Controlled by outer `WHERE` only.
_Avoid_: "filtering by field" for something that must affect which rows appear.

**Value sourcing**:
Stage 2: how one output field gets a value for a surviving row. Controlled by field expressions such as `TEXT(...)` / `ATTR(...)`.
_Avoid_: describing a `NULL` field as "removing the row".

**Row node**:
The current node of the outer scan for a given row.

**Supplier node**:
The node a field expression selects as the value source for one output field. Field predicates inside `TEXT/ATTR/PROJECT` pick suppliers, not rows.

**Row scope vs field scope**:
The two failure categories used everywhere in the helper: row-scope means the wrong rows survived; field-scope means rows are correct but fields are `NULL` or wrong. Diagnose row scope before field scope.

**Axis**:
A named traversal universe for structural queries: `parent`, `child`, `ancestor`, `descendant`. `EXISTS(...)` must declare an axis; a vague axis name is a parse error.

**Node stream**:
The row set produced by a source (`doc` or a derived table). Field extraction works over the row node plus its descendants.

### Sources

**doc / document**:
The canonical source names for the parsed input HTML. Both are valid in `FROM`; `doc` is the shorthand form.

**RAW(...)**:
Source constructor for inline HTML strings, e.g. `FROM RAW('<div>...')`.

**PARSE(...)**:
Source constructor that parses an HTML string — scalar or subquery-produced — into a queryable node stream. Canonical successor to the removed `FRAGMENTS(...)`.
_Avoid_: `FRAGMENTS(...)` (removed).

**CTE**:
A named `WITH` subquery, evaluated in order with statement-local scope.

**Derived table**:
A subquery used as a source: `FROM (SELECT ...) AS alias`.

**LATERAL**:
`CROSS JOIN LATERAL (SELECT ...) AS alias`: a correlated per-left-row expansion.

### Query constructs

**PROJECT(tag) AS (alias: expr, ...)**:
Schema-construction form that pins each named column to a field expression, making the extraction contract explicit and stable. Picks candidates by tag, outer `WHERE` filters rows, field `WHERE` picks values.

**FLATTEN**:
Discovery-oriented extraction with ordered descendant text slices. `FLATTEN_TEXT` and `FLATTEN` are aliases; `FLATTEN_EXTRACT` is a compatibility alias of `PROJECT`.

**EXISTS(axis WHERE ...)**:
Structural row filter used in outer `WHERE` when supplier existence must affect row inclusion.

**TEXT / DIRECT_TEXT / ATTR / INNER_HTML / RAW_INNER_HTML**:
Field extraction functions. `TEXT` aggregates text in the node scope; `DIRECT_TEXT` excludes nested-element text; `INNER_HTML` returns minified inner HTML (raw variant preserves markup); depth slicing applies only to inner-HTML forms.

**SELECT self**:
The canonical way to return the current row node. Legacy `SELECT <from_alias>` still works but is warned on.
_Avoid_: `SELECT <from_alias>` for "current row node".

**Extraction guard**:
`TEXT()` / `INNER_HTML()` require an outer `WHERE` that includes a non-tag self predicate (not only `tag = ...`).

**attr.foo shorthand**:
`attr.foo` is equivalent to `attributes.foo`, including alias/axis-qualified forms like `parent.attr.foo`.

### Output and export

**TO clause (sink)**:
Terminal output/export clause: `TO LIST`, `TO TABLE`, `TO CSV`, `TO PARQUET`, `TO JSON`, `TO NDJSON`. Parquet requires Arrow support; URL sources require curl.

**TableResult / TableOptions**:
`TO TABLE` extraction output and its trimming/sparse options (`TRIM_EMPTY_ROWS`, `TRIM_EMPTY_COLS`, `EMPTY_IS`, `STOP_AFTER_EMPTY_ROWS`, `FORMAT`, `SPARSE_SHAPE`, `HEADER_NORMALIZE`).

**ExportSink**:
Describes an export target (kind + path) so the CLI can write files without re-parsing the query.

### Diagnostics

**lint**:
A parse + validate-only check that returns structured diagnostics without executing the query (`--lint`, `--format json`).

**Diagnostic**:
A structured problem with `severity`, a stable code, `message`, optional `help`, `doc_ref`, and byte/line-column spans plus caret snippets.

**Canonical query formatting**:
The deterministic presentation of a parsed MarkQL query. It preserves query meaning and literal values for review and reuse; it is distinct from syntax migration, which is an explicit language-change operation.

### Subsystems

**markql core**:
The C++20 engine (`core/`). Public API in `core/include/markql/`. Parser must not depend on executor/runtime modules; AST is the shared contract; CLI must only call public APIs.

**markql CLI**:
The `markql` binary (`cli/`). One-shot query/lint/template-render modes plus an interactive REPL.

**markql-agent**:
A localhost HTTP service (`127.0.0.1:7337`) exposing `/health` and `/v1/query`, token-gated, used by the browser plugin. Shipped as a standalone binary that prints a generated token on startup unless `MARKQL_AGENT_TOKEN` is set.

**Browser plugin**:
The Chrome extension (`browser_plugin/extension/`) that captures the page DOM (top document plus accessible frames) and queries the agent. The companion `browser_plugin/agent/` is the C++ agent server.

**Python package**:
The `markql` Python package (`python/markql/`) with pybind bindings (`_core.cpp`) and a CLI-fallback path. Published to PyPI under the legacy name `pyxsql`.
_Avoid_: "pyxsql" in new code/docs except for that intentional package identifier.

**helper**:
The bounded suggestion system (`core/src/helper/` + `python/markql/helper/`): `suggest` / `repair` / `explain` return one next MarkQL query, driven by a deterministic C++ controller with optional model assistance.

**MarkQL AI Lab**:
The experimental context for developing and evaluating models that translate extraction intent and page evidence into MarkQL. It is not part of the helper runtime. Lives in the sibling `markql-ai-lab` repository, which consumes this one; nothing here depends on it.
_Avoid_: AI discovery, model lab, helper model

**MarkQL synthesis**:
The learning task of producing exactly one MarkQL query, and no tool or control action, from extraction intent, page evidence, and explicit constraints.
_Avoid_: AI discovery, code completion

**Synthesis correctness**:
A synthesized query is correct when it is valid MarkQL and returns the requested schema and ordered typed rows defined by the independent oracle. Reference-query text equality is diagnostic, not correctness.
_Avoid_: exact-match accuracy

**Synthesis attempt**:
One MarkQL query proposed by the model and the resulting local validation feedback.

**Synthesis episode**:
A bounded sequence of synthesis attempts for one extraction intent and one page snapshot. It ends on synthesis correctness or when its attempt budget is exhausted.
_Avoid_: browsing session

**Attempt feedback**:
The parser, lint, and execution evidence returned after a synthesis attempt. It excludes independent-oracle values and comparisons.

**Independent oracle**:
The training-and-evaluation authority for the requested schema and ordered typed rows. It determines reward and correctness but is never page evidence or attempt feedback.
_Avoid_: model feedback, answer hint

**Page evidence**:
A versioned, bounded representation of a page snapshot containing structural, content, and attribute evidence needed for MarkQL synthesis. It excludes oracle information and unstable node identifiers.
_Avoid_: raw website, full HTML, helper artifact

**Extraction request**:
A structured natural-language contract with `Records`, `Fields`, and optional `Context`. It defines what each output row represents, the output field names and types, and the semantic constraints for choosing values.
_Avoid_: prompt, unrestricted prose

**Extraction intent**:
The deterministic parsed form of an extraction request, preserving its requested records, typed fields, required or optional values, and semantic context.
_Avoid_: model interpretation, generated query

**Output schema**:
The typed record shape derived from an extraction request's `Fields`, including field names, value types, and required or optional values.
_Avoid_: independently supplied schema, inferred schema

**Semantic binding**:
The setup-time association between one requested output field and a structural value candidate in page evidence. A binding defines reusable extraction logic; it is not a per-row or per-page model decision.
_Avoid_: extracted value, model-generated query

**Record binding**:
The setup-time association between the requested records and the structural page family that produces one output row. It establishes row scope before semantic bindings are chosen for fields within each row.
_Avoid_: field binding, page-type match

**Supervised bootstrap**:
The initial learning phase that teaches MarkQL synthesis and repair from verified examples before verifier rewards are introduced.

**Repair example**:
A verified training transition from a failed MarkQL query and its attempt feedback to a corrected query for the same extraction intent and page snapshot.
_Avoid_: synthetic error

**Verifier-guided learning**:
A post-bootstrap learning phase in which independent-oracle outcomes reward generated queries without exposing oracle information to the model.
_Avoid_: oracle prompting

**Verifier reward**:
A private, shaped measure of synthesis progress whose unique maximum requires synthesis correctness. Partial validity or result agreement can improve reward but can never equal a correct query.
_Avoid_: lint score, oracle feedback

**Page acquisition**:
The controlled creation of a local page snapshot from an external page. Acquisition is strictly outside learning and evaluation loops.
_Avoid_: live training, model browsing

**Page snapshot**:
An immutable local HTML document used by the MarkQL AI Lab. Learning and evaluation operate on snapshots, never directly on external pages.
_Avoid_: live page, website input

**Artifact (helper)**:
A local inspection snapshot at one detail level, ordered by escalation: `compact_families` → `families` → `skeleton` → `targeted_subtree` → `full_html`. The Python helper defaults to native structural evidence for families and native query execution for skeletons; an external inspector is opt-in. Distinct from the removed `.mqd` / `.mqp` serialized-file "artifact" feature.
_Avoid_: using "artifact" for serialized query/document files (removed).

**html_inspector**:
The experimental Rust tool (`tools/html_inspector/`) for interactive inspection and prototype family/skeleton reports. It is not required by the Python library. Native structural discovery is owned by `markql::inspect_html`, exposed as `markql.inspect`; see `docs/structural-inspection.md` for the versioned evidence contract and limitations.

**Retrieval pack**:
A small doc-grounded snippet bundle (topic, facts, verified examples, doc refs) shipped to the model; the helper never sends the full manual.

**Plugin**:
A dynamic library loaded into the CLI/agent through a C ABI (`core/include/markql/plugin_api.h`) to register commands/tokenizers — e.g. `khmer_segmenter`, `number_to_khmer`.
_Avoid_: confusing plugin binaries with the removed "artifact" feature.

**Template rendering**:
Opt-in Jinja2 rendering of `.mql.j2` query files with TOML vars (`--render j2`, `--vars`, `--rendered-out`).

**FetchPolicy**:
The Python loader's network/resource guard: `allow_network`, `allow_private_network`, `timeout`, `max_bytes`; blocks private/localhost targets unless allowed.

### Workflows

**First query loop**:
The canonical debugging routine: inspect rows (`SELECT * ... LIMIT`), gate rows (`WHERE`/`EXISTS`), extract one value, then scale to a full schema. Documented as Chapter 3 of the book.

**Alias convention (style)**:
Use `node_<semantic>` for DOM node aliases and `r_<semantic>` for CTE/derived-row aliases. Style recommendation, not a language rule.

**Two-stage debugging order**:
When output looks wrong, ask in order: "Did the right rows survive?" then "Did each field pick the right supplier?"
