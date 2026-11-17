Feature: Letters: the menu claims few letters, a letter nobody claims does nothing, none acts inside a question (T153 S6; T228: no Quick tab, no page letters on the generic Home)

  Scenario: a letter that nobody claims does nothing
    Given the menu is open on F5
    When I note the status line
    And I press Q
    And I press R
    And I press P
    Then no dialog is open
    And the status line is as noted
    And the area is "C64"

  Scenario: R on Home opens no dialog, the Reset row resets and closes the menu
    Given the menu is open
    When I press R
    Then no dialog is open
    And the area is "Home"
    When I press RETURN
    Then the menu is closed
    When the menu is open
    Then the status line is ""

  Scenario: the footer names the keys of the page, not what the rail and the tab strip show (T228 R11)
    Given the menu is open
    Then the footer shows "UP/DOWN  CHOOSE   RETURN  DO IT   ESC  BACK"
    And the footer does not show "AREA"
    And the footer does not show "TAB"
    When I press F5
    Then the footer shows "UP/DOWN  CHOOSE   RETURN  DO IT   ESC  BACK"
    And the footer does not show "AREA"
    And the footer does not show "TAB"
    When I press F3
    Then the footer does not show "TAB"

  Scenario: no letter acts inside a question
    Given the menu is open on F3
    When I open the medium "USB0"
    And I move to the file "games"
    And I press RETURN
    And I move to the file "alpha.d64"
    And I press RETURN
    And I press D
    And I press R
    And I press P
    Then a question is open: "Delete alpha.d64?"
    When I press left-arrow
    Then no dialog is open
    And the status line is "Cancelled"
