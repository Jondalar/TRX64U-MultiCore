Feature: The operations and states the application exports are the golden list a menu.toml is checked against (T153 D3, the ABI between TOML and C++)

  Scenario: the golden is what the build exports
    Then the build exports the operations and states of "tests/coreapp/golden/ops_trx64u.txt"
