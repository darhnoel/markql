# MarkQL HTML Inspector Prototype

Minimal Rust terminal UI for inspecting HTML structure while drafting MarkQL queries.
The page fills the terminal, and the inspector appears as a popup overlay when a text span is selected.
The inspector shows a tree-style DOM path from `html` down to the selected element.

Build:

```bash
cargo build --manifest-path tools/html_inspector/Cargo.toml
```

Run with a local file:

```bash
cargo run --manifest-path tools/html_inspector/Cargo.toml -- docs/fixtures/basic.html
```

Run with a URL:

```bash
cargo run --manifest-path tools/html_inspector/Cargo.toml -- https://example.com
```

Print a folded structural skeleton:

```bash
cargo run --manifest-path tools/html_inspector/Cargo.toml -- --skeleton https://example.com
```

Analyze repeated row-like families for AI-assisted MarkQL drafting:

```bash
cargo run --manifest-path tools/html_inspector/Cargo.toml -- --families https://example.com
```

Print a compact AI-oriented family summary:

```bash
cargo run --manifest-path tools/html_inspector/Cargo.toml -- --families-compact https://example.com
```

Compact family output is documented in [docs/compact_family_mode.md](./docs/compact_family_mode.md).
The low-token AI workflow is documented in [docs/ai_inspection_playbook.md](./docs/ai_inspection_playbook.md).
The strict MarkQL agent contract is documented in [docs/ai_markql_musts.md](./docs/ai_markql_musts.md).

Keys:

- `Up` / `Down`: move between lines
- `Left` / `Right`: move within a line
- `Tab`: jump to the next visible text span
- `Shift+Tab`: jump to the previous visible text span
- `Enter`: show or hide the inspector popup for the current span
- Left click: select a text span and open the inspector popup
- When the inspector is open: `i` toggles between element details and formatted `inner_html`
- When the inspector is open: `Up` / `Down` scroll vertically inside the popup
- When the inspector is open: `Left` / `Right` scroll horizontally inside the popup
- `PageUp` / `PageDown`: fast vertical scrolling inside the popup
- `Esc`: close the inspector popup
- `q`: quit
