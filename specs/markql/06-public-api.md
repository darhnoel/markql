# Public API

Status: draft skeleton

This file defines public API result contracts for C++, Python, and API consumers.

## API-001: QueryResult

`QueryResult` remains the core execution result contract.

Normative details to fill:

- columns
- rows
- tables
- warnings
- export sink metadata
- table options

## API-002: Rectangular Result View

The public API should expose a stable rectangular result view layered over `QueryResult`.

Python adapters should include:

```python
result.to_records()
result.to_columns()
result.to_pandas()
```

`to_pandas()` is optional and must not make pandas or NumPy a hard dependency of the core package.

## API-003: Prepared Documents

Prepared document reuse remains part of the public execution API.

Normative lifetime and thread-safety details remain to be filled.

