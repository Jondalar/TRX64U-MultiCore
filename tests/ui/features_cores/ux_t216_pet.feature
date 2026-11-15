# core: pet
Feature: T216 UX review pictures of the PET F5 and F7 (ux-specialist; emulator pictures, nothing asserted on pixels)

  Scenario: F5 top, end, a pad row changed
    Given the menu is open on F5
    Then I save the picture "ux-pet-f5-top"
    When I press cursor-down 30 times
    Then I save the picture "ux-pet-f5-end"
    When I press home
    And I press cursor-down 12 times
    And I press cursor-right 3 times
    Then I save the picture "ux-pet-f5-padrow"
    When I press cursor-down 5 times
    And I press cursor-right
    Then I save the picture "ux-pet-f5-padmap"

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

  Scenario: F7
    Given the menu is open on F7
    Then I save the picture "ux-pet-f7"

  Scenario: F1 and F8
    Given the menu is open
    Then I save the picture "ux-pet-f1"

  Scenario: Input and Video tabs
    Given the menu is open on F5 tab 4
    Then I save the picture "ux-pet-f5-input"
    When I press 2
    Then I save the picture "ux-pet-f5-video"
