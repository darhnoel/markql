Feature: CLI script mode error handling
  Scenario: Script execution stops on first error by default
    Given the HTML fixture "docs/fixtures/basic.html"
    And the MarkQL query file "tests/fixtures/queries/script_stops_on_first_error.sql"
    When I run the MarkQL CLI
    Then the exit code is 1
    And stderr contains "statement 2/3"
    And stderr contains "ERROR[MQL-SYN-0001]"
