Feature: Navigation of the TRX64U menu (T153 S1-S3; T228 the generic core menu: areas F1-F5, F7, F8, tabs 1-8, left arrow one step out, RUN/STOP closes)

  Background:
    Given the menu is open

  Scenario: the menu opens on Home
    Then the area is "Home"
    And the page title is "Home"
    And the layout is "list"
    And the activity is "FROZEN"
    And the footer on the left shows "R/S  CLOSE"

  Scenario: the function keys go to their areas
    When I press F3
    Then the area is "Library"
    And the page title is "Library"
    And the layout is "library"
    When I press F5
    Then the area is "C64"
    And the page title is "Machine"
    And the layout is "list"
    When I press F7
    Then the area is "APP"
    And the page title is "Settings"
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
    Then the area is "Monitor"
    And the layout is "terminal"
    When I press 2
    Then the layout is "terminal"
    When I press RUN/STOP
    Then the menu is closed

  Scenario: digits choose the tab of the area
    When I press F5
    And I press 3
    Then the tab is "Memory and I/O"
    And the tab number is 3
    And the page title is "Memory and I/O"
    When I press 5
    Then the tab is "Audio"
    When I press 1
    Then the tab is "Machine"

  Scenario: a digit without a tab does nothing
    When I press F5
    And I press 2
    And I press 8
    Then the tab is "TRX64"

  Scenario: an area comes back on the tab it was left on
    When I press F5
    And I press 3
    And I press F1
    And I press F5
    Then the tab is "Memory and I/O"

  Scenario: left arrow is one step out: tab, area, close
    When I press F5
    And I press 3
    And I press left-arrow
    Then the area is "C64"
    And the tab is "Machine"
    When I press left-arrow
    Then the area is "Home"
    When I press left-arrow
    Then the menu is closed

  Scenario: RUN/STOP closes the menu from a settings page
    When I press F5
    And I press 3
    And I press RUN/STOP
    Then the menu is closed

  Scenario: RUN/STOP closes the menu with a question open
    When I press F3
    And I open the medium "USB0"
    And I move to the file "games"
    And I press RETURN
    And I move to the file "alpha.d64"
    And I press RETURN
    And I press D
    Then a question is open: "Delete alpha.d64?"
    When I press RUN/STOP
    Then the menu is closed

  Scenario: the menu opens on Home again
    When I press F5
    And I press 4
    And I press RUN/STOP
    And the menu is open
    Then the area is "Home"
