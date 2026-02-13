# 02: Row Stability + Debugging

Where this module fits in the pipeline: this module hardens row selection before extraction. You focus on stable structural evidence, not cosmetic classes.

## Story Context

Your first query worked yesterday, but today's page variant returns either empty results or extra rows. This module teaches the recovery loop used in real maintenance work.

## Mission

By the end of this module, you can diagnose and fix row-set failures without rewriting everything.

## Goal

Learn to debug:

- empty results
- noisy results
- over-filtering and under-filtering

## Task list

- `tasks/01_task.md` - baseline candidate rows
- `tasks/02_task.md` - stable row narrowing by attributes
- `tasks/03_task.md` - require price existence
- `tasks/04_task.md` - remove teaser rows
- `tasks/05_task.md` - text-normalized pinpoint filter
- `tasks/06_task.md` - final stable row gate

## Debug this broken query (6 drills)

1. Broken:
```sql
SELECT section(node_id, tag)
FROM doc
WHERE attributes.data-testid = 'offer';
```
Fix idea: wrong row tag assumption; probe with `div` first.

2. Broken:
```sql
SELECT div(node_id)
FROM doc
WHERE attributes.class = 'offer-card';
```
Fix idea: class churn risk; anchor on `data-testid`.

3. Broken:
```sql
SELECT div(node_id)
FROM doc
WHERE attributes.data-kind = 'gold';
```
Fix idea: includes teaser variants; require structural evidence.

4. Broken:
```sql
SELECT div(node_id)
FROM doc
WHERE attributes.data-testid = 'offer'
  AND attributes.data-kind = 'gold'
  AND text LIKE '%price-main%';
```
Fix idea: this text condition is brittle; use `EXISTS(descendant ...)`.

5. Broken:
```sql
SELECT div(node_id)
FROM doc
WHERE attributes.data-testid = 'offer'
  AND EXISTS(descendant WHERE tag = 'span');
```
Fix idea: too broad. Match specific class intent.

6. Broken:
```sql
SELECT div(node_id)
FROM doc
WHERE attributes.data-testid = 'offer'
  AND EXISTS(child WHERE attributes.class CONTAINS 'price-main');
```
Fix idea: price node may be nested deeper; use `descendant`.

## Decision checkpoint

1. Which is more stable: cosmetic class fragments or `data-testid` + structure?
2. When should you use `EXISTS(descendant ...)`?
3. If rows are noisy, do you add more output columns first or harden row filters first?

Answers:

1. `data-testid` + structure.
2. When row inclusion depends on nested evidence.
3. Harden row filters first.

## What you can do now

- debug row-set problems systematically
- choose stable row anchors
- encode structural requirements with `EXISTS`
