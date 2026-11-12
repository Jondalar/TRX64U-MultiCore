Feature: The Library starts on the list of the media, like the host's File Browser (T104.2, Alex 2026-10-29)

  Scenario: open the Library, see the media, choose the SD card, ../ leads back to the list
    Given the menu is open on F3
    Then the media list is up
    And the library lists "SD, USB0"
    And the medium "SD" is listed with "FAT"
    And the medium "USB0" is listed with "FAT"
    And the footer shows "RETURN  OPEN IT"
    When I open the medium "SD"
    Then the library is at "/SD"
    And the library lists "cards, sdhello.prg"
    And the focused item is ".."
    When I press RETURN
    Then the media list is up
    And the library lists "SD, USB0"

  Scenario: the panel at the media level tells what the selected medium is
    Given the menu is open on F3
    Then the media list is up
    When I move to the medium "USB0"
    Then the info panel says of "USB0": Contents "FAT"
    And the info panel says of "USB0": Access "readable"
    When I move to the medium "SD"
    Then the info panel says of "SD": Size "B"
    And the info panel says of "SD": Contents "FAT"

  Scenario: the arrow keys at the top of a medium lead to the list, and nowhere above it
    When I open the medium "USB0"
    Then the library is at "/USB0"
    When I press left-arrow
    Then the media list is up
    When I open the medium "USB0"
    And I press cursor-left
    Then the media list is up
    When I press cursor-left
    Then the media list is up
    And the status line is ""

  Scenario: the left-arrow key at the media level leaves the Library, as it leaves any page
    Given the menu is open on F3
    When I press left-arrow
    Then the area is "Home"
