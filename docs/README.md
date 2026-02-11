# MarkQL Docs

- Canonical tutorial: [markql-tutorial.md](markql-tutorial.md)
- Book (chapter path): [book/SUMMARY.md](book/SUMMARY.md)
- Verify book examples: [verify_examples.sh](verify_examples.sh)
- Case studies: [case-studies/README.md](case-studies/README.md)
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
