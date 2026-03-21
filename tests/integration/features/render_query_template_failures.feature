Feature: CLI Jinja2 query rendering failures are reported cleanly
  Scenario: Unknown render mode is rejected
    Given the MarkQL query file "tests/fixtures/render/golden_query.mql.j2"
    And additional CLI args "--render liquid --rendered-out -"
    When I run the MarkQL CLI
    Then the exit code is 2
    And stdout is empty
    And stderr contains "Invalid --render value"

  Scenario: Missing vars fail with strict undefined behavior
    Given the MarkQL query file "tests/fixtures/render/golden_query.mql.j2"
    And additional CLI args "--render j2 --rendered-out -"
    When I run the MarkQL CLI
    Then the exit code is 2
    And stdout is empty
    And stderr contains "Missing template variable"
    And stderr does not contain "Query parse error"

  Scenario: Invalid TOML fails before MarkQL parsing
    Given the MarkQL query file "tests/fixtures/render/golden_query.mql.j2"
    And additional CLI args "--render j2 --vars tests/fixtures/render/invalid_vars.toml --rendered-out -"
    When I run the MarkQL CLI
    Then the exit code is 2
    And stdout is empty
    And stderr contains "Invalid TOML vars file"
    And stderr does not contain "Query parse error"

  Scenario: Invalid Jinja syntax fails before MarkQL parsing
    Given the MarkQL query file "tests/fixtures/render/invalid_template.mql.j2"
    And additional CLI args "--render j2 --vars tests/fixtures/render/golden_query.toml --rendered-out -"
    When I run the MarkQL CLI
    Then the exit code is 2
    And stdout is empty
    And stderr contains "Invalid Jinja2 template syntax"
    And stderr does not contain "Query parse error"

  Scenario: Stdout preview is rejected with lint mode
    Given the MarkQL query file "tests/fixtures/render/golden_query.mql.j2"
    And additional CLI args "--render j2 --vars tests/fixtures/render/golden_query.toml --rendered-out - --lint"
    When I run the MarkQL CLI
    Then the exit code is 2
    And stdout is empty
    And stderr contains "--rendered-out - is not supported with --lint"
