Feature: CLI invalid query reports diagnostics
  Scenario: Parser diagnostic is returned on invalid query
    Given the HTML fixture "docs/fixtures/basic.html"
    And the MarkQL query "SELECT div FROM document WHERE attributes.id = ;"
    When I run the MarkQL CLI
    Then the exit code is 1
    And stdout is empty
    And stderr contains "ERROR[MQL-SYN-0001]"
    And stderr contains "Missing scalar expression"
