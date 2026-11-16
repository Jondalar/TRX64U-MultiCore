# core: pet
Feature: T216 UX review pictures of the PET menu (ux-specialist; emulator pictures, nothing asserted on pixels)

  Scenario: F5 PET tab top and end
    Given the menu is open on F5
    Then I save the picture "ux-pet-f5-top"
    When I press cursor-down 30 times
    Then I save the picture "ux-pet-f5-end"

  Scenario: SuperPET
    Given the menu is open on F5
    When I set "Model" to "SuperPET"
    Then I save the picture "ux-pet-f5-superpet"
    When I press cursor-down 30 times
    Then I save the picture "ux-pet-f5-superpet-end"

  Scenario: 2001 + BASIC 2
    Given the menu is open on F5
    When I set "Model" to "2001 + BASIC 2"
    Then I save the picture "ux-pet-f5-2001b2"

  Scenario: Video tab
    Given the menu is open on F5 tab 2
    Then I save the picture "ux-pet-f5-video"

  Scenario: Input tab
    Given the menu is open on F5 tab 4
    Then I save the picture "ux-pet-f5-input"
    When I press cursor-down 3 times
    Then I save the picture "ux-pet-f5-input-row"
    When I press cursor-down 4 times
    Then I save the picture "ux-pet-f5-input-pad"
    When I press cursor-down 2 times
    Then I save the picture "ux-pet-f5-input-map"

  Scenario: Keyboard mapping positional
    Given the menu is open on F5 tab 4
    When I set "Keyboard mapping" to "Positional (PET key place)"
    Then I save the picture "ux-pet-f5-input-positional"

  Scenario: F7
    Given the menu is open on F7
    Then I save the picture "ux-pet-f7"

  Scenario: F1
    Given the menu is open
    Then I save the picture "ux-pet-f1"
