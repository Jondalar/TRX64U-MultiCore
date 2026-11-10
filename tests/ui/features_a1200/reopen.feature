# core: amiga1200
Feature: Where the menu opens (T192.3 D30): Home (F1) by default, or the screen it was left on

  Scenario: by default the menu opens on Home, wherever it was left
    Given the menu is open on F5 tab 2
    When I press RUN/STOP
    Then the menu is closed
    When the menu is open
    Then the area key is F1

  Scenario: Last screen brings the area, the tab and the row back
    Given the menu is open on F8
    When I set "Menu opens at" to "Last screen"
    And I press F5
    And I press 2
    And I move to "Chipset"
    And I press RUN/STOP
    Then the menu is closed
    When the menu is open
    Then the area key is F5
    And the tab number is 2
    And the focused item is "Chipset"
    And the status line is ""

  Scenario: Last screen brings the Library folder and the file back
    Given the menu is open on F3
    When I open the medium "USB0"
    And I move to the file "games"
    And I press RETURN
    And I move to the file "beta.adf"
    And I press RUN/STOP
    Then the menu is closed
    When the menu is open
    Then the area key is F3
    And the library is at "/USB0/games"
    And the focused item is "beta.adf"

  Scenario: Home (F1) puts the menu on Home again
    Given the menu is open on F8
    When I set "Menu opens at" to "Home (F1)"
    And I press F5
    And I press 2
    And I press RUN/STOP
    And the menu is open
    Then the area key is F1
