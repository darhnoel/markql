Feature: CLI valid query returns rows
  Scenario: Selecting result cards from a local fixture succeeds
    Given the HTML fixture "docs/fixtures/basic.html"
    And the MarkQL query "SELECT section FROM document WHERE attributes.class CONTAINS 'result' ORDER BY node_id ASC LIMIT 2;"
    When I run the MarkQL CLI
    Then the exit code is 0
    And stdout contains "Rows: 2"
    And stdout contains "section"
    And stderr is empty
