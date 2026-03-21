Feature: CLI Jinja2 query rendering succeeds
  Scenario: Existing plain query-file execution remains unchanged
    Given the HTML fixture "docs/fixtures/basic.html"
    And the MarkQL query file "tests/fixtures/render/plain_query.mql"
    When I run the MarkQL CLI
    Then the exit code is 0
    And stdout contains "Rows: 2"
    And stdout contains "section"
    And stderr is empty

  Scenario: Existing plain query-file lint remains unchanged
    Given the MarkQL query file "tests/fixtures/render/plain_query.mql"
    And additional CLI args "--lint"
    When I run the MarkQL CLI
    Then the exit code is 0
    And stdout contains "Result: 0 error(s), 0 warning(s), 0 note(s)"
    And stderr is empty

  Scenario: Rendered query lint succeeds
    Given the MarkQL query file "tests/fixtures/render/golden_query.mql.j2"
    And additional CLI args "--render j2 --vars tests/fixtures/render/golden_query.toml --lint"
    When I run the MarkQL CLI
    Then the exit code is 0
    And stdout contains "Result: 0 error(s), 0 warning(s), 0 note(s)"
    And stderr is empty

  Scenario: Rendered query executes against a local fixture
    Given the HTML fixture "tests/fixtures/render/generic_table.html"
    And the MarkQL query file "tests/fixtures/render/generic_query.mql.j2"
    And additional CLI args "--render j2 --vars tests/fixtures/render/generic_query.toml"
    When I run the MarkQL CLI
    Then the exit code is 0
    And stdout contains "R-100"
    And stdout contains "Growth Plan"
    And stdout contains "Rows: 2"
    And stderr is empty

  Scenario: Rendered output file is written without changing lint behavior
    Given the MarkQL query file "tests/fixtures/render/golden_query.mql.j2"
    And additional CLI args "--render j2 --vars tests/fixtures/render/golden_query.toml --rendered-out /tmp/markql_rendered_query.mql --lint"
    When I run the MarkQL CLI
    Then the exit code is 0
    And the file "/tmp/markql_rendered_query.mql" matches golden file "tests/golden/render/golden_query.mql"
    And stdout contains "Result: 0 error(s), 0 warning(s), 0 note(s)"
    And stderr is empty

  Scenario: Rendered stdout preview matches the golden query exactly
    Given the MarkQL query file "tests/fixtures/render/golden_query.mql.j2"
    And additional CLI args "--render j2 --vars tests/fixtures/render/golden_query.toml --rendered-out -"
    When I run the MarkQL CLI
    Then the exit code is 0
    And stdout matches golden file "tests/golden/render/golden_query.mql"
    And stderr is empty
