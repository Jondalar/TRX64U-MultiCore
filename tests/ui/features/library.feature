Feature: The Library: media, folders, the sheet of a file, marks (T153 S7)

  Background:
    Given the menu is open on F3
    And I go up to the top of the medium "USB0"

  Scenario: the Library opens on the stick
    Then the layout is "library"
    And the library is at "/USB0"
    And the library lists "games, roms"
    And the focused item is ".."

  Scenario: RETURN opens a folder and the left arrow goes back up
    When I move to the file "games"
    And I press RETURN
    Then the library is at "/USB0/games"
    And the library lists "alpha.d64, beta.d64, hello.prg"
    When I press left-arrow
    Then the library is at "/USB0"

  Scenario: RETURN on a disk image opens its sheet, and every item has a letter
    When I move to the file "games"
    And I press RETURN
    And I move to the file "alpha.d64"
    And I press RETURN
    Then the sheet is open for "alpha.d64"
    And the sheet lists "Mount disk" with the letter M
    And the sheet lists "Run disk" with the letter R
    And the sheet lists "Delete" with the letter D
    When I press left-arrow
    Then no sheet is open
    And the library is at "/USB0/games"

  Scenario: a letter of the sheet runs its item
    When I move to the file "games"
    And I press RETURN
    And I move to the file "alpha.d64"
    And I press RETURN
    And I press M
    Then no sheet is open
    And the status line is "Mount disk: alpha.d64"

  Scenario: SPACE marks a file and goes on to the next
    When I move to the file "games"
    And I press RETURN
    And I move to the file "alpha.d64"
    And I press SPACE
    Then the file "alpha.d64" is marked
    And the file "beta.d64" is not marked
    And the focused item is "beta.d64"
    When I press SPACE
    Then the file "beta.d64" is marked

  Scenario: Ctrl-N takes every mark away, Ctrl-A marks every file
    When I move to the file "games"
    And I press RETURN
    And I press ctrl+n
    Then the file "alpha.d64" is not marked
    And the file "beta.d64" is not marked
    When I press ctrl+a
    Then the file "alpha.d64" is marked
    And the file "hello.prg" is marked

  # The tab "Online" is the online search sub-app's (apps/online, T239.3 L-3): the core's own record has Browse and Search, the sub-app adds tab 3 when the .cor carries online.u2a.
  Scenario: digits choose Search
    When I press 2
    Then the tab is "Search"
