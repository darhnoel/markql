Feature: CLI JSON output contract
  Scenario: JSON export mode is deterministic for fixture query
    Given the HTML fixture "docs/fixtures/basic.html"
    And the MarkQL query "SELECT section FROM document WHERE attributes.class CONTAINS 'result' ORDER BY node_id ASC LIMIT 2 TO JSON();"
    When I run the MarkQL CLI
    Then the exit code is 0
    And stderr is empty
    And stdout matches golden file "tests/golden/integration/cli_json_output_is_stable.golden"
