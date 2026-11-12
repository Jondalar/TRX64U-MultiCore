# core: amiga1200
Feature: Pictures of the T212 areas of the amiga1200 core: F1 with its drive list (empty and mounted), the filtered mount, F5, F7 (APP), F8 (emulator pictures for the UX look, nothing asserted on pixels)

  Scenario: F1 and the list of an empty drive
    Given the menu is open
    Then I save the picture "t212-amiga1200-f1-home"
    When I move to "DF0"
    And I press RETURN
    Then I save the picture "t212-amiga1200-f1-drive-empty"

  Scenario: the mount from the drive list shows the files of the drive only
    When I move to "Mount from Library..."
    And I press RETURN
    Then I save the picture "t212-amiga1200-f3-mount-media"
    When I open the medium "USB0"
    And I move to the file "games"
    And I press RETURN
    Then I save the picture "t212-amiga1200-f3-filtered-mount"
    When I move to the file "alpha.adf"
    And I press RETURN
    Then I save the picture "t212-amiga1200-f1-mounted"

  Scenario: the mounted drive's list
    Given the menu is open on F1
    When I move to "DF0"
    And I press RETURN
    Then I save the picture "t212-amiga1200-f1-drive-mounted"

  Scenario: Eject from the drive list: Home with the toast
    Given the menu is open on F1
    When I move to "DF0"
    And I press RETURN
    And I move to "Eject"
    And I press RETURN
    Then I save the picture "t212-amiga1200-f1-ejected"

  Scenario: F5, F7 and F8
    Given the menu is open on F5
    Then I save the picture "t212-amiga1200-f5"
    When I press F7
    Then I save the picture "t212-amiga1200-f7-app"
    When I press F8
    Then I save the picture "t212-amiga1200-f8"
