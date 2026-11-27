# core: amiga1200
Feature: The operations and states the Amiga 1200 application exports are its golden list (T153 D3, the ABI between TOML and C++; T192.3 section 5)

  Scenario: the golden is what the build exports
    Then the build exports the operations and states of "cores/amiga1200/app/tests/golden/ops_amiga1200.txt"
