# core: amiga1200
Feature: The Library of the Amiga 1200: media, folders, the sheet of a file, marks (T153 S7 on the A1200)

  Background:
    Given the menu is open on F3
    And I go up to the top of the medium "USB0"

  Scenario: the Library opens on the stick
    Then the layout is "list"
    And the library is at "/USB0"
    And the library lists "cores, games"
    And the focused item is ".."

  Scenario: RETURN opens a folder and the left arrow goes back up
    When I move to the file "games"
    And I press RETURN
    Then the library is at "/USB0/games"
    And the library lists "alpha.adf, beta.adf, hello.prg"
    When I press left-arrow
    Then the library is at "/USB0"

  Scenario: RETURN on a disk image opens its sheet, and the items have their letters
    When I move to the file "games"
    And I press RETURN
    And I move to the file "alpha.adf"
    And I press RETURN
    Then the sheet is open for "alpha.adf"
    And the sheet lists "Mount DF0"
    And the sheet lists "Mount DF3"
    And the sheet lists "View" with the letter T
    And the sheet lists "Delete" with the letter D
    When I press left-arrow
    Then no sheet is open
    And the library is at "/USB0/games"

  Scenario: a letter of the sheet runs its item
    When I move to the file "games"
    And I press RETURN
    And I move to the file "alpha.adf"
    And I press RETURN
    And I press D
    Then no sheet is open
    And a question is open: "Delete alpha.adf?"
    When I press left-arrow
    Then no dialog is open
    And the status line is "Cancelled"
    And the library lists "alpha.adf, beta.adf, hello.prg"

  Scenario: RETURN on the first row of the sheet mounts the disk in DF0
    When I move to the file "games"
    And I press RETURN
    And I move to the file "alpha.adf"
    And I press RETURN
    And I press RETURN
    Then no sheet is open
    And the status line is "Mount DF0: alpha.adf"

  Scenario: SPACE marks a file and goes on to the next
    When I move to the file "games"
    And I press RETURN
    And I move to the file "alpha.adf"
    And I press SPACE
    Then the file "alpha.adf" is marked
    And the file "beta.adf" is not marked
    And the focused item is "beta.adf"
    When I press SPACE
    Then the file "beta.adf" is marked

  Scenario: Ctrl-N takes every mark away, Ctrl-A marks every file
    When I move to the file "games"
    And I press RETURN
    And I press ctrl+n
    Then the file "alpha.adf" is not marked
    And the file "beta.adf" is not marked
    When I press ctrl+a
    Then the file "alpha.adf" is marked
    And the file "hello.prg" is marked

  Scenario: a digit without a tab does nothing in the Library
    When I press 2
    Then the tab is "Browse"
    When I press 3
    Then the tab is "Browse"
    And the library is at "/USB0"
