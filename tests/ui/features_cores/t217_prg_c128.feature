# core: c128
Feature: T217 program rows on c128 (ux-review pictures; emulator pictures for the UX look, nothing asserted on pixels)

  Scenario: a .prg in the stick's folder, its sheet
    Given the menu is open on F3
    And I go up to the top of the medium "USB0"
    When I move to the file "games"
    And I press RETURN
    Then I save the picture "t217-c128-folder"
    When I move to the file "hello.prg"
    Then I save the picture "t217-c128-prg-selected"
    When I press RETURN
    Then I save the picture "t217-c128-prg-sheet"

  Scenario: a .d64 opened, a .prg inside, its sheet
    Given the menu is open on F3
    And I go up to the top of the medium "USB0"
    When I move to the file "games"
    And I press RETURN
    And I move to the file "alpha.d64"
    And I press RETURN
    Then I save the picture "t217-c128-d64-sheet"
    When I move to the file "Browse Disk"
    And I press RETURN
    Then I save the picture "t217-c128-d64-inside"
    When I move to the file "HELLO.prg"
    Then I save the picture "t217-c128-d64-inner-selected"
    When I press RETURN
    Then I save the picture "t217-c128-d64-inner-sheet"
