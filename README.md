<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/assets/logo/markql_logo_dark.svg">
    <source media="(prefers-color-scheme: light)" srcset="docs/assets/logo/markql_logo_light.svg">
    <img src="docs/assets/logo/markql_logo_light.svg" alt="MarkQL logo" width="220">
  </picture>
</p>

<h1 align="center">MarkQL</h1>
<p align="center">SQL-style query engine for HTML</p>

<p align="center">
  <a href="https://github.com/darhnoel/markql/actions/workflows/python-wheels.yml"><img src="https://github.com/darhnoel/markql/actions/workflows/python-wheels.yml/badge.svg" alt="Build wheels"></a>
  <a href="https://github.com/darhnoel/markql/tags"><img src="https://img.shields.io/github/v/tag/darhnoel/markql" alt="GitHub tag"></a>
  <a href="https://pypi.org/project/pyxsql/"><img src="https://img.shields.io/pypi/v/pyxsql" alt="PyPI version"></a>
  <a href="https://pypi.org/project/pyxsql/"><img src="https://img.shields.io/pypi/dm/pyxsql" alt="PyPI downloads"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-Apache%202.0-blue.svg" alt="License"></a>
</p>

MarkQL is a **SQL-style query engine for HTML** that lets you **select precisely what you need**, **filter to the relevant parts of a page**, and **extract structured fields** using the familiar `SELECT ... FROM ... WHERE ...` flow, rather than relying on brittle, ad-hoc scraping logic.

## Demo Video

<p align="center">
  <img src="docs/assets/demo/markql-demo.gif" alt="MarkQL demo GIF" width="640">
</p>

## Quick Start

Prerequisites:
- CMake 3.16+
- A C++20 compiler
- Boost (multiprecision); set `-DXSQL_ENABLE_KHMER_NUMBER=OFF` to skip Boost
- Optional dependencies: `libxml2`, `curl`, `nlohmann_json`, `arrow/parquet`

Ubuntu/Debian/WSL (minimal packages):

```bash
sudo apt update
sudo apt install -y \
  git ca-certificates pkg-config \
  build-essential cmake ninja-build \
  libboost-dev
```

Optional feature packages:

```bash
sudo apt install -y libxml2-dev libcurl4-openssl-dev nlohmann-json3-dev
```

Arrow/Parquet packages (often missing on older distros):

```bash
sudo apt install -y libarrow-dev libparquet-dev
```

macOS (Homebrew):

```bash
xcode-select --install
brew install cmake ninja pkg-config boost
```

Optional feature packages:

```bash
brew install libxml2 curl nlohmann-json
```

Arrow/Parquet:

```bash
brew install apache-arrow
```

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
  -DXSQL_WITH_NLOHMANN_JSON=OFF
cmake --build build
```

To build without Boost, add `-DXSQL_ENABLE_KHMER_NUMBER=OFF`.

Run one query:

```bash
./build/markql --query "SELECT div FROM doc LIMIT 5;" --input ./data/index.html
```

Run interactive REPL:

```bash
./build/markql --interactive --input ./data/index.html
```

## CLI Notes

- Primary CLI binary is `./build/markql`.
- Legacy compatibility binary `./build/xsql` is still generated.
- `doc` and `document` are both valid sources in `FROM`.
- If `--input` is omitted, the CLI reads HTML from `stdin`.
- URL sources (`FROM 'https://...'`) require `XSQL_WITH_CURL=ON`.
- `TO PARQUET(...)` requires `XSQL_WITH_ARROW=ON`.
- `INNER_HTML(...)` returns minified HTML by default. Use `RAW_INNER_HTML(...)` for unmodified raw output.
- `TO TABLE(...)` supports explicit trimming/sparse options: `TRIM_EMPTY_ROWS`, `TRIM_EMPTY_COLS`, `EMPTY_IS`, `STOP_AFTER_EMPTY_ROWS`, `FORMAT`, `SPARSE_SHAPE`, and `HEADER_NORMALIZE`.

## Testing

C++ tests:

```bash
cmake --build build --target xsql_tests
ctest --test-dir build --output-on-failure
```

Python package/tests (optional):

```bash
./install_python.sh
./test_python.sh
```

## Documentation

- Book (chapter path + verified examples): [docs/book/SUMMARY.md](docs/book/SUMMARY.md)
- Canonical tutorial: [docs/markql-tutorial.md](docs/markql-tutorial.md)
- CLI guide: [docs/markql-cli-guide.md](docs/markql-cli-guide.md)
- Docs index: [docs/README.md](docs/README.md)
- Changelog: [CHANGELOG.md](CHANGELOG.md)

## License

Apache License 2.0. See [LICENSE](LICENSE).
