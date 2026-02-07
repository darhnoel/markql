# XSQL Docs

- Tutorial: [xsql-tutorial.md](xsql-tutorial.md)
- CLI guide: [xsql-cli-guide.md](xsql-cli-guide.md)
- Changelog: [../CHANGELOG.md](../CHANGELOG.md)

## Syntax Diagrams

This folder also contains auto-generated railroad diagrams for the XSQL grammar.

Generate them with:

```bash
python3 -m pip install -r docs/requirements.txt
python3 docs/generate_diagrams.py
# or
./docs/build_diagrams.sh
```

Output files are written to `docs/diagrams/`.
