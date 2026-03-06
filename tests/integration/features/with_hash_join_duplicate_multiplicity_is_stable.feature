Feature: Hash join duplicate multiplicity contract
  Scenario: Hash join preserves duplicate multiplicity for equi-join keys
    Given the HTML fixture "tests/fixtures/joins/hash_join_duplicate_keys.html"
    And the MarkQL query:
      """
      WITH left_vals AS (
        SELECT n.text AS k
        FROM doc AS n
        WHERE n.tag = 'x'
      ),
      right_vals AS (
        SELECT n.text AS k, n.node_id AS rid
        FROM doc AS n
        WHERE n.tag = 'y'
      )
      SELECT l.k, r.rid
      FROM left_vals AS l
      JOIN right_vals AS r ON l.k = r.k
      ORDER BY l.k, r.rid
      TO JSON();
      """
    When I run the MarkQL CLI
    Then the exit code is 0
    And stderr is empty
    And stdout matches golden file "tests/golden/integration/with_hash_join_duplicate_multiplicity_is_stable.golden"
