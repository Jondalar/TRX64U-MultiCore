# core: amiga1200
Feature: The Library of the Amiga 1200 starts on the list of the media, like the host's File Browser (T104.2, T153 on the A1200)

  Scenario: open the Library, see the media, choose the SD card, ../ leads back to the list
    Given the menu is open on F3
    Then the media list is up
    And the library lists "SD, Flash, USB0"
    And the medium "SD" is listed with "FAT"
    And the medium "USB0" is listed with "FAT"
    And the footer shows "RETURN"
    When I open the medium "SD"
    Then the library is at "/SD"
    And the library lists "cards, sdhello.prg"
    And the focused item is ".."
    When I press RETURN
    Then the media list is up
    And the library lists "SD, Flash, USB0"

  Scenario: the panel at the media level tells what the selected medium is
    Given the menu is open on F3
    Then the media list is up
    When I move to the medium "USB0"
    Then the info panel says of "USB0": Contents "FAT"
    And the info panel says of "USB0": Access "readable"
    When I move to the medium "SD"
    Then the info panel says of "SD": Size "B"
    And the info panel says of "SD": Contents "FAT"
    When I move to the medium "Flash"
    Then the info panel says of "Flash": Contents "FAT12"

  Scenario: the left arrow at the top of a medium leads to the list
    When I open the medium "USB0"
    Then the library is at "/USB0"
    When I press left-arrow
    Then the media list is up
    When I open the medium "USB0"
    And I press left-arrow
    Then the media list is up

  Scenario: the left-arrow key at the media level leaves the Library, as it leaves any page
    Given the menu is open on F3
    When I press left-arrow
    Then the area is "Home"
