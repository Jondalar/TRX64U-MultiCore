Feature: Letters: a page letter beats a core hotkey, a hotkey acts where the page does not claim it (T153 S6; T104.2 button letters 2026-10-29)

  Scenario: R on the drive page is the page's Reset drive, not the core's Reset
    Given the menu is open on F5
    When I press R
    Then no dialog is open
    And the status line says "drive"

  Scenario: R on Home is the core's Reset, which asks
    Given the menu is open
    When I press R
    Then a question is open: "Reset the machine?"
    When I press left-arrow
    Then no dialog is open
    And the status line is "Cancelled"

  Scenario: X on the drive page is the core's Remove Cartridge: the page does not claim X
    Given the menu is open on F5
    When I press X
    Then the status line is "There is no cartridge in the port"

  Scenario: a greyed page button shows its reason instead of running
    Given the menu is open on F5
    When I press E
    Then the status line is "There is no disk in the drive"
    When I press F6
    And I press S
    Then the status line says "Nothing to save"

  Scenario: a letter that nobody claims does nothing
    Given the menu is open on F5
    When I press Q
    Then no dialog is open
    And the status line is ""

  Scenario: the footer names the letters of the page
    Given the menu is open
    Then the footer shows "R RESET"
    And the footer shows "P PAUSE"
    When I press F6
    Then the footer shows "D DISCARD"
    And the footer shows "S SAVE"

  Scenario: no letter acts inside a question
    Given the menu is open
    When I press R
    And I press P
    Then a question is open: "Reset the machine?"
    When I press left-arrow
    Then no dialog is open
