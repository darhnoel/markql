# Canonical query formatting is separate from syntax migration

Status: accepted

MarkQL formatting will be a deterministic canonical representation of a parsed query, owned by the core language layer and exposed through thin CLI, Python, editor, and webapp adapters. Formatting preserves literal values and query meaning; it does not silently modernize legacy syntax. Syntax migration remains a separate, explicit tool because combining presentation with language changes would make generated and user-authored queries harder to review safely.

The first stable formatter must cover every parseable AST variant, fail clearly for unsupported nodes during development, and satisfy both parse-after-format and execution-equivalence tests. In the Intelligence product, formatting is presentation-only: extraction executes the validated generated query, while the canonical form is used for display, copying, and diagnostics.
