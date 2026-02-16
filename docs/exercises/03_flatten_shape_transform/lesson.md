# 03: FLATTEN as a Shape Transform

Where this module fits in the pipeline: this module comes after stable row filtering. It is used when nested repetition must be exploded into columns.

## Story Context

Some pages contain repeated nested lists (items, tags, badge stacks). You need a fast way to inspect that repeated shape, but you do not want to confuse this with semantic extraction.

## Mission

By the end of this module, you can decide when FLATTEN helps and when to stop using it.

## Goal

Use FLATTEN only for repeated nested shape.

Important warning: FLATTEN is not the default tool for all extraction. Use it when repetition is the core problem.

## Task list

- `tasks/01_task.md` - flatten shipment rows into positional chunks
- `tasks/02_task.md` - flatten line-items containers into positional chunks
- `tasks/03_task.md` - flatten each `li` into item-level columns

## When FLATTEN is NOT the right tool

- when you need named semantic fields with stable meaning
- when different card variants have optional/missing blocks
- when positional columns become unclear for readers

## Decision checkpoint

1. What problem does FLATTEN solve best?
2. Should FLATTEN be your first choice for every extraction?
3. If output columns drift by position, what does that tell you?

Answers:

1. Nested repeated shape expansion.
2. No.
3. The structure likely needs a different extraction strategy.

## What you can do now

- recognize true FLATTEN use cases
- apply FLATTEN deliberately to repeated nested lists
- avoid overusing positional expansion
