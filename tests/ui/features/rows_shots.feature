Feature: Pictures of the list rows of the TRX64U core (no mode choice in the emulator: its own 1080p) (T209 ux-review; emulator pictures for the UX look, nothing asserted on pixels)

  Scenario: Library with the long list
    Given the menu is open on F3
    And I go up to the top of the medium "USB0"
    Then I save the picture "t209-lib-root"
    When I move to the file "big"
    And I press RETURN
    Then I save the picture "t209-lib-big-top"
    When I press cursor-down 6 times
    Then I save the picture "t209-lib-big-6"
    When I press cursor-down 8 times
    Then I save the picture "t209-lib-big-14"
    When I press +
    Then I save the picture "t209-lib-big-page"
    When I press cursor-down 40 times
    Then I save the picture "t209-lib-big-end"

  Scenario: Home, media slots and settings
    Given the menu is open on F1
    Then I save the picture "t209-f1-home"
    Given the menu is open on F5
    Then I save the picture "t209-f5-top"
    When I press cursor-down 6 times
    Then I save the picture "t209-f5-6"
    When I press cursor-down 40 times
    Then I save the picture "t209-f5-end"
    Given the menu is open on F8
    Then I save the picture "t209-f8"

  Scenario: The APP settings (F7)
    Given the menu is open on F7
    Then I save the picture "t209-f7-top"
    When I press cursor-down 4 times
    Then I save the picture "t209-f7-4"
    When I press cursor-down 8 times
    Then I save the picture "t209-f7-12"
