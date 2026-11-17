# core: nes
Feature: The Picture page of the NES (T187 D5): Video > Picture opens every mode at 50 and 60 Hz (the screen's modes marked), browsing applies nothing, RETURN asks, LEFT goes back (pictures from the emulator)

  Background:
    Given the menu is open on F5 tab 2

  Scenario: the Video page, the Picture row opens the page and the cursor starts on the mode in force
    Then I save the picture "nes-f5-video"
    When I move to "Picture"
    And I press RETURN
    Then the status line says "RETURN tries"
    And I save the picture "nes-picture-page"

  Scenario: browsing the list applies nothing, RETURN on a mode asks, LEFT goes back at once
    When I move to "Picture"
    And I press RETURN
    And I press cursor-down
    And I save the picture "nes-picture-browse"
    And I press RETURN
    And I save the picture "nes-picture-question"
    Then the picture "nes-picture-question" has text in the box
    And I press cursor-left
    And I save the picture "nes-picture-reverted"
    And I press cursor-left
    Then the page title is "Video"
    And the status line is ""
