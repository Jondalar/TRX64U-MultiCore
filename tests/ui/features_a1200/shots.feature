# core: amiga1200
Feature: Pictures of the reworked menu areas of the Amiga 1200 (a core with media) (T187 D1, D2, D7, D8, T205 D1; emulator pictures for the UX look, nothing asserted on pixels)

  Scenario: Home (with the media slots), the core's settings, the end of the list
    Given the menu is open
    Then I save the picture "amiga1200-f1-home"
    When I press F5
    Then I save the picture "amiga1200-f5-first-tab"
    When I press cursor-down 30 times
    Then I save the picture "amiga1200-f5-settings-end"

  Scenario: F5 with Video, Audio, Input and the Hotkeys page (T205 D1: no F6)
    Given the menu is open on F5 tab 2
    Then I save the picture "amiga1200-f5-video"
    When I press 3
    Then I save the picture "amiga1200-f5-audio"
    When I press 4
    Then I save the picture "amiga1200-f5-input"
    When I press F7
    And I move to "Hotkeys"
    And I press RETURN
    Then I save the picture "amiga1200-f7-hotkeys"

  Scenario: the System area F8 with its groups
    Given the menu is open on F8
    Then I save the picture "amiga1200-f8-system"
