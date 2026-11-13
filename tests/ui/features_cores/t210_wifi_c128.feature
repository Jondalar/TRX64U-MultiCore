# core: c128
Feature: F8 WiFi row of the c128 core shows the module's state, not "No module" (T210 S2; emulator picture, the state is read off the picture)

  Scenario: the System area F8
    Given the menu is open on F8
    Then the page has the items "WiFi"
    And I save the picture "t210-c128-f8"

  Scenario: RETURN on the WiFi row toggles it
    Given the menu is open on F8
    When I move to "WiFi"
    And I press RETURN
    Then the status line says "WiFi is off"
    And I press cursor-down
    And I press cursor-up
    And I save the picture "t210-c128-f8-toggled"

  Scenario: a second RETURN switches it on again (the session keeps the radio off from the scenario before)
    Given the menu is open on F8
    When I move to "WiFi"
    And I press RETURN
    Then the status line says "WiFi is on"
    And I press cursor-down
    And I press cursor-up
    And I save the picture "t210-c128-f8-on-again"
