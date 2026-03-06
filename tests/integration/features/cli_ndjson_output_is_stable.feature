Feature: CLI NDJSON output contract
  Scenario: NDJSON export mode is deterministic for fixture query
    Given the HTML fixture "docs/fixtures/basic.html"
    And the MarkQL query:
      """
      SELECT section.node_id, section.tag
      FROM document
      WHERE attributes.class CONTAINS 'result'
      ORDER BY node_id ASC
      LIMIT 2
      TO NDJSON();
      """
    When I run the MarkQL CLI
    Then the exit code is 0
    And stderr is empty
    And stdout matches golden file "tests/golden/integration/cli_ndjson_output_is_stable.golden"
