# core: cpc
Feature: Pictures of the CPC F1 drive list (T92 D-17b): an empty A: mounts with the reset, a filled A: replaces without it; the .dsk action sheet (emulator pictures, nothing asserted on pixels)

  Scenario: F1 and the list of an empty drive
    Given the menu is open
    Then I save the picture "t92-cpc-f1-home"
    When I move to "A:"
    And I press RETURN
    Then I save the picture "t92-cpc-f1-drive-empty"

  Scenario: the mount from the empty drive's list
    When I move to "Mount from Library..."
    And I press RETURN
    Then I save the picture "t92-cpc-f3-mount-media"
    When I open the medium "USB0"
    And I move to the file "games"
    And I press RETURN
    And I move to the file "alpha.dsk"
    And I press RETURN
    Then I save the picture "t92-cpc-f1-mounted"

  Scenario: the action sheet of a .dsk in the Library
    Given the menu is open on F3
    When I open the medium "USB0"
    And I move to the file "games"
    And I press RETURN
    And I move to the file "beta.dsk"
    And I press RETURN
    Then I save the picture "t92-cpc-f3-dsk-sheet"

  Scenario: the filled drive's list
    Given the menu is open on F1
    When I move to "A:"
    And I press RETURN
    Then I save the picture "t92-cpc-f1-drive-filled"

  Scenario: the Library from the filled drive's list
    When I move to "Replace from Library..."
    And I press RETURN
    Then I save the picture "t92-cpc-f3-replace-media"
