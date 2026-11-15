# core: nes
Feature: NES - the pad mapping on the Input tab (T226 S2)

  Scenario: Input tab and Actions
    Given the menu is open on F5 tab 4
    Then I save the picture "t226-nes-f5-input"
    When I press F7
    Then I save the picture "t226-nes-f7-actions"
