# core: amiga1200
Feature: The Home page and the media rows of the Amiga 1200 (T153 S8 on the A1200: no desk, Home has a row per drive since T205)

  Scenario: Home lists what the machine is and has a row for each drive
    Given the menu is open
    Then the layout is "list"
    And the page has the items "Core, Application, Network, Media, ROMs, Libraries"
    And the page has the items "DF0, DF1, DF2, DF3, HD0, HD1"

  Scenario: an empty drive says so
    Given the menu is open
    Then the item "DF0" shows "No image"
    And the item "DF3" shows "No image"
    And the item "HD0" shows "No image"

  Scenario: the settings the machine is configured with show on the settings tab
    Given the menu is open on F5 tab 1
    Then the setting "Chip RAM" is "2M"
    And the setting "Chipset" is "AGA"
    And the setting "Floppy drives" is "2"

  Scenario: a disk mounted from the Library shows on Home
    Given the menu is open on F3
    When I open the medium "USB0"
    And I move to the file "games"
    And I press RETURN
    And I move to the file "alpha.adf"
    And I press RETURN
    And I press RETURN
    And I press F1
    Then the item "DF0" shows "alpha.adf"

  Scenario: RETURN on a mounted drive row ejects it, and the status line is gone when the menu is closed and opened again
    Given the menu is open
    When I move to "DF0"
    And I press RETURN
    Then the status line is "DF0 ejected"
    And the item "DF0" shows "No image"
    When I press RUN/STOP
    Then the menu is closed
    When the menu is open
    Then the status line is ""
