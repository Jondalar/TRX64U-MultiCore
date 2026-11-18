# core: nes
Feature: Pictures of the reworked menu areas of the NES (a core with no disks) (T187 D1, D2, D7, D8, T205 D1; emulator pictures for the UX look, nothing asserted on pixels)

  Scenario: Home, the core's settings, the end of the list
    Given the menu is open
    Then I save the picture "nes-f1-home"
    When I press F5
    Then I save the picture "nes-f5-first-tab"
    When I press cursor-down 30 times
    Then I save the picture "nes-f5-settings-end"

  Scenario: F5 with Video, Audio, Input and the Hotkeys page (T205 D1: no F6)
    Given the menu is open on F5 tab 2
    Then I save the picture "nes-f5-video"
    When I press 3
    Then I save the picture "nes-f5-audio"
    When I press 4
    Then I save the picture "nes-f5-input"

  Scenario: the System area F8 with its groups
    Given the menu is open on F8
    Then I save the picture "nes-f8-system"
