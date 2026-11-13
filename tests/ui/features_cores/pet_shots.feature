# core: pet
Feature: The Library sheet of a PET disk image offers Run Disk beside the two mounts (T206 ux-review; emulator picture, nothing asserted on pixels)

  Scenario: the sheet of a .d64
    Given the menu is open on F3
    And I go up to the top of the medium "USB0"
    When I move to the file "games"
    And I press RETURN
    And I move to the file "alpha.d64"
    And I press RETURN
    Then the sheet is open for "alpha.d64"
    And the sheet lists "Run Disk"
    And I save the picture "pet-f3-sheet-d64"
