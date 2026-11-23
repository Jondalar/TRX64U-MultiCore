# core: trx64u+ovtest
Feature: A library's element over the running picture of a C64-type core, and the library's calls menu_hide and menu_show (T256 S3, S9, S10; the test library of tests/ui/ovtest, not TRX64-Ultimate's ring library)

  Scenario: S3 - the element stays over the running picture while the menu is closed, and the picture moves under it
    Given the menu is open
    When I press RUN/STOP
    And the test library shows its element
    And I save the picture "overlay-s3-a"
    And the picture "overlay-s3-a" shows the library's element at its place
    And I save the picture "overlay-s3-b"
    And I save the picture "overlay-s3-c"
    Then the picture "overlay-s3-b" shows the library's element at its place
    And the picture "overlay-s3-b" shows another running picture than "overlay-s3-a" or than "overlay-s3-c" above the element

  Scenario: S9 - while the menu is open the element is not drawn, and it is back when the menu closes
    Given the test library shows its element
    When the menu is open
    And I save the picture "overlay-s3-menu-open"
    Then the picture "overlay-s3-menu-open" shows no element of the library
    When I press RUN/STOP
    And I save the picture "overlay-s3-menu-closed"
    Then the picture "overlay-s3-menu-closed" shows the library's element at its place

  Scenario: the element goes when the library takes it away
    Given the test library takes its element away
    When I save the picture "overlay-s3-removed"
    Then the picture "overlay-s3-removed" shows no element of the library

  Scenario: S10 - the library hides the menu as the person closes it, and shows it as the menu key does
    Given the menu is open
    When the test library hides the menu
    Then the menu is closed
    When the test library shows the menu
    Then the menu is shown
    When the test library hides the menu
    Then the menu is closed

  Scenario: S10 - after hide the menu key brings the menu back
    Given the menu is open
    When the test library hides the menu
    Then the menu is closed
    When the menu is open
    Then the menu is shown

  Scenario: S10 - the library paused the machine first: hide leaves it standing, the PAUSED tag and the still show
    Given the test library pauses the machine
    When the menu is open
    And the test library hides the menu
    Then the menu is closed
    When I save the picture "overlay-s10-paused"
    Then the picture "overlay-s10-paused" shows the PAUSED tag at its place
    And the picture "overlay-s10-paused" shows the machine's last frame behind the tag, not black
