# core: vc20+ovtest
Feature: A library's element on a core that draws its picture on the pixel layer (T256 S5, D1): none while the machine runs, shown over the kept picture while it stands (the test library of tests/ui/ovtest)

  Scenario: S5 - a running layer core shows no element
    Given the menu is open
    When I press RUN/STOP
    And the test library shows its element
    And I save the picture "overlay-s5-running"
    Then the picture "overlay-s5-running" shows no element of the library

  Scenario: S5 - a standing layer core keeps its picture and shows the element and the tag over it
    Given the test library takes its element away
    And the test library paints the machine's picture
    And the menu is closed
    When I save the picture "overlay-s5-before"
    And the test library shows its element
    And the test library pauses the machine
    And I save the picture "overlay-s5-standing"
    Then the picture "overlay-s5-standing" shows the library's element at its place
    And the picture "overlay-s5-standing" shows the PAUSED tag at its place
    And the picture "overlay-s5-standing" is the picture "overlay-s5-before" outside the library's element and the tag
    And the emulator log has no line with "no still"
    And the emulator log has no line with "no memory for the still"

  Scenario: S5 - the element goes with the machine's Resume
    Given the test library resumes the machine
    When I save the picture "overlay-s5-resumed"
    Then the picture "overlay-s5-resumed" shows no element of the library
    And the picture "overlay-s5-resumed" shows no tag

  Scenario: S10 - the library hides and shows the menu on a layer core
    Given the menu is open
    When the test library hides the menu
    Then the menu is closed
    When the test library shows the menu
    Then the menu is shown
