Feature: CLI rejects conflicting query inputs
  Scenario: --query and --query-file are mutually exclusive
    Given the HTML fixture "docs/fixtures/basic.html"
    And the MarkQL query "SELECT section FROM document LIMIT 1;"
    And additional CLI args "--query-file tests/fixtures/queries/mju_krerng_ingredients.sql"
    When I run the MarkQL CLI
    Then the exit code is 2
    And stdout is empty
    And stderr contains "mutually exclusive"
