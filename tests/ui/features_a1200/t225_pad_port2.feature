# core: amiga1200
Feature: T225 - a USB gamepad as game port 2 of the Amiga (the Input tab rows; pictures for the UX look, the emulator has no USB gamepad to attach)

  Scenario: the Input tab with the mapping Off, then On with the target choice
    Given the menu is open on F5 tab 4
    Then the page has the items "Map port 2 to USB controller"
    And the page has the items "Port 2 acts as, Map port 2 buttons"
    And I save the picture "t225-input-off"
    When I move to "Map port 2 to USB controller"
    And I press RETURN
    Then the setting "Map port 2 to USB controller" is "On"
    And I save the picture "t225-input-on"
    When I step "Port 2 acts as" up 2 times
    Then the setting "Port 2 acts as" is "CD32 gamepad"
    And I save the picture "t225-input-cd32"


  Scenario: Map port 2 buttons with no pad plugged opens the dialog and asks for a pad (D3)
    Given the menu is open on F5 tab 4
    When I move to "Map port 2 buttons"
    And I press RETURN
    And I save the picture "t225-map-no-pad"
