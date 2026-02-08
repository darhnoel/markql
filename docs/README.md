# MarkQL Docs

- Tutorial: [markql-tutorial.md](markql-tutorial.md)
- CLI guide: [markql-cli-guide.md](markql-cli-guide.md)
- Changelog: [../CHANGELOG.md](../CHANGELOG.md)

## Syntax Diagrams

This folder also contains auto-generated railroad diagrams for the MarkQL grammar.

Generate them with:

```bash
python3 -m pip install -r docs/requirements.txt
python3 docs/generate_diagrams.py
# or
./docs/build_diagrams.sh
```

Output files are written to `docs/diagrams/`.
