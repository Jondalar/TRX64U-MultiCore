Feature: The overlay service of the base (T256 D1-D10): the PAUSED tag over the machine's last frame, the open menu over the still, the person's Overlay Transparency (S2, S4, S9)

  Scenario: S4 - a paused C64-type machine shows its last frame behind the tag, not black, and the tag says only PAUSED
    Given the machine is paused with the menu closed
    Then I save the picture "overlay-paused-trx64u"
    And the picture "overlay-paused-trx64u" shows the PAUSED tag at its place
    And the picture "overlay-paused-trx64u" shows the machine's last frame behind the tag, not black

  Scenario: S9 - the open menu covers the tag, the still stays under it, and the tag is back when the menu closes
    Given the machine is paused with the menu closed
    When I press F1
    Then the menu is shown
    And I save the picture "overlay-paused-menu-open"
    And the picture "overlay-paused-menu-open" shows no tag
    When I press RUN/STOP
    Then I save the picture "overlay-paused-menu-closed-again"
    And the picture "overlay-paused-menu-closed-again" shows the PAUSED tag at its place

  Scenario: the tag goes when the machine runs again
    Given the machine is paused with the menu closed
    When the machine runs again
    Then I save the picture "overlay-resumed"
    And the picture "overlay-resumed" shows no tag

  Scenario: S2 - the menu opens with the Overlay Transparency the person set, also after the tag was up
    Given the menu is open on F7
    And I set "Overlay Transparency" to "0 %"
    And I save the picture "overlay-alpha-0"
    When I set "Overlay Transparency" to "75 %"
    And I press RUN/STOP
    And the machine is paused with the menu closed
    And I press F7
    Then the menu is shown
    And I save the picture "overlay-alpha-75-after-tag"
    And the picture "overlay-alpha-75-after-tag" differs in brightness from "overlay-alpha-0"
