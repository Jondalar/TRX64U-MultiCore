# core: amiga1200
Feature: Pictures of the list rows of the Amiga 1200 core (T209 ux-review; emulator pictures for the UX look, nothing asserted on pixels)

  Scenario: Library with the long list
    Given the menu is open on F3
    Then I save the picture "t209-lib-media"
    When I open the medium "USB0"
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

  Scenario: A greyed row with its reason, and a long mounted name
    Given the menu is open on F7
    Then I save the picture "t209-f7-top"
    When I press cursor-down 3 times
    Then I save the picture "t209-f7-3"
    When I press cursor-down 8 times
    Then I save the picture "t209-f7-11"
    Given the menu is open on F3
    When I open the medium "USB0"
    And I move to the file "games"
    And I press RETURN
    And I move to the file "Turrican_II_The_Final_Fight_Director_Cut_Disk_1_of_3_Fixed.adf"
    And I press RETURN
    And I press RETURN
    And I press F1
    Then I save the picture "t209-f1-longname"
