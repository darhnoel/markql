# Non-Goals

Status: draft skeleton

This file records explicit non-goals for the spec-first C++ refactor.

## NONGOAL-001: No Rust Rewrite Now

Rust is deferred optional work. It is not part of the active Phase 2 implementation plan.

## NONGOAL-002: No Public Migration CLI Yet

The migration tool starts as an internal tool under `tools/`. Public CLI migration support is deferred until behavior is proven.

## NONGOAL-003: No Formal Workflow Yet

No repo-grounded formal verification or conformance workflow exists yet. Do not invent a formal-only process in this spec.

## NONGOAL-004: No Dual DOM Backend

The target is one DOM backend: libxml2. The naive parser is not a long-term supported backend.

## NONGOAL-005: No Mandatory NumPy Dependency

Rectangular result APIs should make NumPy and pandas participation easy without making them hard dependencies.

