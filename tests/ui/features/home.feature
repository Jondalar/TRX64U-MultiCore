Feature: The Home media desk (T153 S8; T104.2 P3)

  Background:
    Given the menu is open

  Scenario: the desk has a row for each medium and a block for the machine
    Then the layout is "desk"
    And the item "Disk" is in slot "media"
    And the item "Cartridge" is in slot "media"
    And the item "Program" is in slot "media"
    And the item "Pause / resume" is in slot "machine"
    And the item "Reset" is in slot "machine"
    And the item "Settings" is in slot "machine"

  Scenario: an empty medium says so
    Then the item "Disk" shows "No disk"
    And the item "Cartridge" shows "No cartridge"
    And the item "Program" shows "No program"
    And the item "Settings" shows "Saved"

  Scenario: the machine rows show the settings they are configured with
    Then the item "RAM expansion" shows "Disabled"
    And the item "Turbo" shows "Off"
    And the item "Video" shows "PAL"

  Scenario: a disk mounted from the Library shows on the desk
    When I press F3
    And I open the medium "USB0"
    And I move to the file "games"
    And I press RETURN
    And I move to the file "alpha.d64"
    And I press RETURN
    And I press M
    And I press F1
    Then the item "Disk" shows "alpha.d64"

  Scenario: RETURN on the Disk row opens the Library on its folder
    When I move to "Disk"
    And I press RETURN
    Then the area is "Library"
    And the library is at "/USB0/games"
