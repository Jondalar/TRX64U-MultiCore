# core: c128
Feature: T251 (#121): a core's file picker shows a file's size as a number of bytes - the board's printf has no length modifier (the C128's JiffyDOS ROM row; the RISC-V image in the emulator)

  Scenario: the JiffyDOS C128 ROM row opens the picker; in games/ the files' rows show their sizes
    Given the menu is open on F5
    When I move to "JiffyDOS C128 ROM"
    And I press RETURN
    And I move to the file "games/"
    And I press RETURN
    Then the item "alpha.d64" shows "174848 bytes"
    And the item "hello.prg" shows "14 bytes"
    And I save the picture "t251-c128-pick"
