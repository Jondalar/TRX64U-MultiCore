# core: amiga1200
Feature: A 31-character file name mounted in DF0 (T205 ux-review: the Home value is cut in the middle, the slot label stays)

  Scenario: the long name on Home
    Given the menu is open on F3
    When I open the medium "USB0"
    And I move to the file "games"
    And I press RETURN
    And I move to the file "Turrican_II_The_Final_Fight_Director_Cut_Disk_1_of_3_Fixed.adf"
    And I press RETURN
    And I press RETURN
    And I press F1
    Then I save the picture "a1200-f1-longname"
