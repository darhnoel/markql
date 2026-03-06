Feature: CLI lint JSON diagnostics contract
  Scenario: Lint mode emits stable JSON diagnostic fields
    Given the HTML fixture "docs/fixtures/basic.html"
    And the MarkQL query "SELECT FROM doc"
    And additional CLI args "--lint --format json"
    When I run the MarkQL CLI
    Then the exit code is 1
    And stderr is empty
    And stdout matches golden file "tests/golden/integration/cli_lint_json_diagnostic_is_stable.golden"
