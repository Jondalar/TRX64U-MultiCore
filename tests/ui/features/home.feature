Feature: The Home page and the media rows of the TRX64U core (T153 S8; T228: no desk, Home has the session actions and a row per medium; R1-R3: Timewarp... and Monitor... under the resets, no Pause, no Reboot, no memory saves)

  Background:
    Given the menu is open

  Scenario: Home has the session actions and a row for each medium
    Then the layout is "list"
    And the page has the items "Reset, Timewarp..., Monitor..."
    And the page has no item "Pause / resume"
    And the page has no item "Reboot, clear memory"
    And the page has no item "Save C64 memory"
    And the page has no item "Save REU memory"
    And the page has the items "Drive A:, Cartridge port, Program"

  Scenario: an empty medium says so
    Then the item "Drive A:" shows "No image"
    And the item "Cartridge port" shows "No image"
    And the item "Program" shows "Nothing loaded"

  Scenario: a disk mounted from the Library shows on Home
    When I press F3
    And I open the medium "USB0"
    And I move to the file "games"
    And I press RETURN
    And I move to the file "alpha.d64"
    And I press RETURN
    And I press M
    And I press F1
    Then the item "Drive A:" shows "alpha.d64"

  Scenario: RETURN on the Drive A row opens its list
    When I move to "Drive A:"
    And I press RETURN
    Then the page title is "Drive A"
