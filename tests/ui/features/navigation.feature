Feature: Navigation of the TRX64U menu (T153 S1-S3; T104.2 keys: areas F1-F8, tabs 1-8, left arrow one step out, RUN/STOP closes)

  Background:
    Given the menu is open

  Scenario: the menu opens on Home
    Then the area is "Home"
    And the page title is "Home"
    And the layout is "desk"
    And the activity is "FROZEN"
    And the footer on the left shows "R/S  CLOSE"

  Scenario: the function keys go to their areas
    When I press F3
    Then the area is "Library"
    And the page title is "Library"
    And the layout is "library"
    When I press F5
    Then the area is "C64"
    And the page title is "Drive A"
    And the layout is "split"
    When I press F6
    Then the area is "Settings"
    And the page title is "TRX64 behavior"
    And the layout is "settings"
    When I press F7
    Then the area is "Actions"
    And the page title is "Do the next thing"
    When I press F8
    Then the area is "System"
    And the page title is "System"
    When I press F1
    Then the area is "Home"

  Scenario: F2 is the Timewarp screen
    When I press F2
    Then the area is "Timewarp"
    And the layout is "timeline"

  Scenario: F4 is the monitor, and the monitor keeps every key
    When I press F4
    Then the area is "Tools"
    And the layout is "terminal"
    When I press 2
    Then the layout is "terminal"
    When I press RUN/STOP
    Then the menu is closed

  Scenario: digits choose the tab of the area
    When I press F6
    And I press 3
    Then the tab is "Audio"
    And the tab number is 3
    And the page title is "Audio and SID"
    When I press 5
    Then the tab is "Network"
    When I press 1
    Then the tab is "TRX64"

  Scenario: a digit without a tab does nothing
    When I press F6
    And I press 2
    And I press 8
    Then the tab is "Video"

  Scenario: an area comes back on the tab it was left on
    When I press F6
    And I press 3
    And I press F1
    And I press F6
    Then the tab is "Audio"

  Scenario: left arrow is one step out: tab, area, close
    When I press F6
    And I press 3
    And I press left-arrow
    Then the area is "Settings"
    And the tab is "TRX64"
    When I press left-arrow
    Then the area is "Home"
    When I press left-arrow
    Then the menu is closed

  Scenario: RUN/STOP closes the menu from a settings page
    When I press F6
    And I press 3
    And I press RUN/STOP
    Then the menu is closed

  Scenario: RUN/STOP closes the menu with a question open
    When I press R
    Then a question is open: "Reset the machine?"
    When I press RUN/STOP
    Then the menu is closed

  Scenario: the menu opens on Home again
    When I press F6
    And I press 4
    And I press RUN/STOP
    And the menu is open
    Then the area is "Home"
