# core: vc20
Feature: Pictures of F1, F5 (core, Video, Audio, Input) and F8 of the vc20 core (T205 ux-review; emulator pictures for the UX look, nothing asserted on pixels)

  Scenario: Home, the core's settings and the end of the list
    Given the menu is open
    Then I save the picture "vc20-f1-home"
    When I press F5
    Then I save the picture "vc20-f5-core-top"
    When I press cursor-down 30 times
    Then I save the picture "vc20-f5-core-end"

  Scenario: F5 Video, Audio, Input
    Given the menu is open on F5 tab 2
    Then I save the picture "vc20-f5-video"
    When I press 3
    Then I save the picture "vc20-f5-audio"

  Scenario: the System area F8
    Given the menu is open on F8
    Then I save the picture "vc20-f8-system"
