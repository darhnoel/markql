Feature: CTE and projection interaction
  Scenario: WITH CTE projected fields remain stable
    Given the HTML fixture "docs/fixtures/basic.html"
    And the MarkQL query:
      """
      WITH cards AS (
        SELECT section
        FROM document
        WHERE attributes.class CONTAINS 'card'
      )
      SELECT c.node_id, c.tag
      FROM cards AS c
      ORDER BY node_id ASC
      LIMIT 2
      TO JSON();
      """
    When I run the MarkQL CLI
    Then the exit code is 0
    And stderr is empty
    And stdout matches golden file "tests/golden/integration/cte_projection_interaction_is_stable.golden"
