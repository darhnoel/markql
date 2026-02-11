# Appendix B: Operator Reference

## TL;DR
This is the quick lookup for predicate operators. Prefer this list when you need exact spellings and current behavior.

You can always inspect live support with:

```bash
./build/markql --mode plain --color=disabled --query "SHOW OPERATORS;"
```

Current operators in this build:
- `=` equality
- `<>` inequality
- `<`, `<=`, `>`, `>=` ordered comparison
- `IN (...)` membership
- `LIKE` SQL wildcard (`%`, `_`)
- `CONTAINS`, `CONTAINS ALL`, `CONTAINS ANY`
- `IS NULL`, `IS NOT NULL`
- `HAS_DIRECT_TEXT` (legacy shorthand; prefer `DIRECT_TEXT(tag) LIKE '%...%'`)
- `~` regex match
- `AND`, `OR`

## Practical guidance
- Prefer `LIKE` for text wildcard filtering.
- Prefer `EXISTS(descendant WHERE ...)` for structural gating.
- Keep predicates simple and composable while exploring.
