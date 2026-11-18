# core: amiga1200
Feature: Navigation of the Amiga 1200 menu (T153 S1-S3 on the A1200: areas F1/F3/F5/F7/F8, tabs 1-4, left arrow one step out, RUN/STOP closes)

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
    And the page title is "Browse"
    When I press F5
    Then the area is "A1200"
    And the page title is "A1200"
    When I press F7
    Then the area is "APP"
    And the page title is "Settings"
    When I press F8
    Then the area is "System"
    And the page title is "System"
    When I press F1
    Then the area is "Home"

  Scenario: a function key without an area does nothing (F2 and F4 have no area on the Amiga 1200)
    When I press F5
    And I press F2
    And I press F4
    Then the area is "A1200"
    And the tab is "A1200"

  Scenario: digits choose the tab of the area
    When I press F5
    And I press 2
    Then the tab is "Video"
    And the tab number is 2
    And the page title is "Video"
    When I press 1
    Then the tab is "A1200"

  Scenario: a digit without a tab does nothing
    When I press F5
    And I press 2
    And I press 3
    And I press 8
    Then the tab is "Audio"

  Scenario: an area comes back on the tab it was left on
    When I press F5
    And I press 2
    And I press F1
    And I press F5
    Then the tab is "Video"

  Scenario: left arrow is one step out: tab, area, close
    When I press F5
    And I press 2
    And I press left-arrow
    Then the area is "A1200"
    And the tab is "A1200"
    When I press left-arrow
    Then the area is "Home"
    When I press left-arrow
    Then the menu is closed

  Scenario: RUN/STOP closes the menu from a settings page
    When I press F5
    And I press 2
    And I press RUN/STOP
    Then the menu is closed

  Scenario: RUN/STOP closes the menu with a question open
    When I press F3
    And I open the medium "USB0"
    And I move to the file "games"
    And I press RETURN
    And I move to the file "alpha.adf"
    And I press RETURN
    And I press D
    Then a question is open: "Delete alpha.adf?"
    When I press RUN/STOP
    Then the menu is closed

  Scenario: the menu opens on Home again
    When I press F5
    And I press 2
    And I press RUN/STOP
    And the menu is open
    Then the area is "Home"
