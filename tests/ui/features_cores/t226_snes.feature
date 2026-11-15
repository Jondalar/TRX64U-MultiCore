# core: snes
Feature: SNES - the loading note while the cartridge is read in, the pad mapping on the Input tab (T226 S1, S2; emulator pictures for the UX look)

  Scenario: Input tab and Actions
    Given the menu is open on F5 tab 4
    Then I save the picture "t226-snes-f5-input"
    When I press F7
    Then I save the picture "t226-snes-f7-actions"

  Scenario: the cartridge loads
    Given the menu is open on F3
    And I go up to the top of the medium "USB0"
    When I move to the file "games"
    And I press RETURN
    And I move to the file "test.sfc"
    And I press RETURN
    Then I save the picture "t226-snes-library-actions"
    When I press RETURN and save the picture "t226-snes-loading" while it works
    Then I save the picture "t226-snes-after-load"
