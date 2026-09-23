# Structural inspection

```python
import markql

document = markql.load("saved-page.html")
evidence = markql.inspect(document)
print(evidence.to_json())
```

`inspect` also accepts HTML strings, UTF-8 bytes, and `pathlib.Path`. It never
fetches URLs, executes scripts, invokes a model, or changes `markql.doc`.
Fetch/render separately (for example, with Crawl4AI), then pass the resulting HTML.
The default input limit is 5,000,000 bytes; `max_bytes=` accepts a positive integer.
The existing Python loader also enforces its node limit. Use `b""` for empty HTML.

Run the offline inspection/execution example with:

```sh
PYTHONPATH=python python3 examples/inspection.py
```

## Ownership and extension boundary

The C++ module behind `markql::inspect_html(html)` owns discovery, grouping,
recipes, and sampling. Its only parsing dependency is the existing core DOM
parser. The Python layer loads local inputs and wraps the serialized result;
the binding only translates public C++ values into Python values.

Add new discovery heuristics inside `core/src/inspection/`, tested through the
public API. Do not duplicate discovery in Python or add model-provider code to
the core. Fetchers, models, query validation, and execution are separate consumers.
Inspection and execution use the same configured parser, but currently parse
separately; a `Document` does not yet cache a native DOM.

The default Python helper uses native inspection for compact/full families and
MarkQL execution for a skeleton of up to 1,000 nodes. Explicit `inspector_cmd=`
still opts into the previous external-command interface. The Rust inspector
remains an experimental interactive tool, not a runtime dependency or the
authoritative implementation of this API. Its prototype JSON is not interchangeable
with the native schema, even where names overlap.

## Evidence v1

`to_dict()` returns a detached dictionary; `to_json()` returns equivalent JSON.
The envelope contains `schema: "markql.structural-evidence"`, `schema_version: 1`,
`truncated`, and `record_candidates`.

Each record includes a snapshot-local `id`, observed `count`, `recipe`,
`markers` (currently empty), and `field_candidates`. Recipes contain a tag,
shared class tokens, and optional parent tag/classes. They are hints, not unique
selectors: separate containers may have identical recipes.

Each field includes an opaque snapshot-local `id`, `kind` (`text` or `attribute`),
`recipe`, nullable `attribute`, `samples`, and `path`. Paths are one-based element
child positions relative to the record; `[]` means the record itself. Paths
distinguish repeated sibling tags such as table cells. An observed field need
not exist in every member, and optional markup can shift positions.

Discovery currently requires at least three non-leaf sibling elements sharing
their tag and immediate child-tag sequence. Field candidates are grouped by
relative path, tag, and attribute. Selected attributes are `href`, `src`, `title`,
`id`, `style`, and `aria-label`; text is the core node's aggregate text, not a
promise of the exact value returned by `TEXT(...)`. Script/style/template nodes
are not traversed as field suppliers, but ancestor aggregate text can contain
their content. HTML text and attributes are untrusted data, including when sent
to a model.

Limits: 64 record candidates, 64 fields per record, three distinct sample
prefixes per field, 256 UTF-8 bytes per prefix, depth 32 per record, and 100,000
field-node visits across discovery. `truncated` signals hitting a limit other
than ordinary representative sampling. Counts describe the discovered sibling
group even when its fields are incomplete. Recipes for fields cover observed
suppliers only. These bounds limit evidence collection, not the core parser's
memory/time or total serialized bytes (class tokens remain intact).

Results are deterministic for the same HTML and parser configuration. IDs and
ordering are not durable identifiers; parser backends may repair malformed HTML
differently. Consumers should check the schema version, tolerate additive keys,
and never infer meaning from IDs. Breaking changes to field meanings require a
new schema version.

## What this does not promise

This is structural evidence, not natural-language extraction or a universal
record detector. Singleton records, leaf-only lists, irregular cards, and
JavaScript/canvas-only data may yield no useful candidates. Empty evidence is
not proof that a page contains no data. The existing heuristic helper is only
a row-check fallback, not a general semantic extraction model.

A model can use evidence to propose MarkQL, but the query must still be linted,
executed against the original HTML, and checked for the expected rows/fields.
Inspection does not certify prices, schedules, or any other domain facts.

## Verification

`python/tests/test_inspection.py` exercises the real native binding, local-input
safety, detached serialization, table paths, common recipes, Unicode sample
limits, and the helper's execution path. `tests/test_inspection.cpp` tests the
public C++ boundary and record/field/depth limits. No external websites, model
responses, or Cargo subprocesses are required for the native path.
