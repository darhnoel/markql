Feature: Recipe ingredients extraction
  Scenario: Khmer recipe ingredients are extracted from heading-linked list
    Given the HTML fixture "tests/fixtures/recipes/mju_krerng_recipe.html"
    And the MarkQL query file "tests/fixtures/queries/mju_krerng_ingredients.sql"
    When I run the MarkQL CLI
    Then the exit code is 0
    And stderr is empty
    And stdout matches golden file "tests/golden/integration/recipe_ingredients_extraction_is_stable.golden"
