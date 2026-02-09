# MarkQL

MarkQL is a C++20 SQL-style query engine for HTML. It treats HTML elements as rows and lets you query them with familiar `SELECT ... FROM ... WHERE ...` syntax.

## Quick Start

Prerequisites:
- CMake 3.16+
- A C++20 compiler
- Optional dependencies: `libxml2`, `curl`, `nlohmann_json`, `arrow/parquet`

Build (project default):

```bash
./build.sh
```

Minimal build when optional dependencies are unavailable:

```bash
cmake -S . -B build \
  -DXSQL_WITH_LIBXML2=OFF \
  -DXSQL_WITH_CURL=OFF \
  -DXSQL_WITH_ARROW=OFF \
  -DXSQL_BUILD_AGENT=ON \
  -DXSQL_AGENT_FETCH_DEPS=ON
cmake --build build
```

Run one query:

```bash
./build/markql --query "SELECT div FROM doc LIMIT 5;" --input ./data/index.html
```

Run interactive REPL:

```bash
./build/markql --interactive --input ./data/index.html
```

## Browser Plugin MVP

Build and run `xsql-agent` (localhost `127.0.0.1:7337`):

```bash
./build.sh
./start-agent.sh
```

Notes:
- `XSQL_AGENT_TOKEN` is required by the agent.
- `start-agent.sh` sets a default token if not provided.
- You can set your own token:

```bash
XSQL_AGENT_TOKEN=your-secret-token ./start-agent.sh
```

Load the Chrome extension:
1. Open `chrome://extensions`
2. Enable `Developer mode`
3. Click `Load unpacked`
4. Select `browser_plugin/extension`

Extension host permission:
- `http://127.0.0.1:7337/*`

## CLI Notes

- Primary CLI binary is `./build/markql`.
- Legacy compatibility binary `./build/xsql` is still generated.
- `doc` and `document` are both valid sources in `FROM`.
- If `--input` is omitted, the CLI reads HTML from `stdin`.
- URL sources (`FROM 'https://...'`) require `XSQL_WITH_CURL=ON`.
- `TO PARQUET(...)` requires `XSQL_WITH_ARROW=ON`.
- `INNER_HTML(...)` returns minified HTML by default. Use `RAW_INNER_HTML(...)` for unmodified raw output.

## Testing

C++ tests:

```bash
cmake --build build --target xsql_tests
ctest --test-dir build --output-on-failure
```

Benchmark harness (inner_html minified vs raw):

```bash
./build/markql_bench_inner_html 10000
```

Python package/tests (optional):

```bash
./install_python.sh
./test_python.sh
```

## Documentation

- Tutorial: [docs/markql-tutorial.md](docs/markql-tutorial.md)
- CLI guide (intro to advanced): [docs/markql-cli-guide.md](docs/markql-cli-guide.md)
- Syntax diagrams and generator: [docs/README.md](docs/README.md)
- Changelog: [CHANGELOG.md](CHANGELOG.md)

## License

Apache License 2.0. See [LICENSE](LICENSE).
