# no-emulator
Feature: The pack-time letter check (T153 S9; tools/descrec.py)

  Scenario: two buttons of one page may not claim one letter
    Given a menu description with the page "F6/trx64"
    And the page has the button "Save" with the bkey S
    And the page has the button "Share" with the bkey S
    When I pack it
    Then packing is refused with "the letter S is claimed by"

  Scenario: the same letter on two pages is fine
    Given a menu description with the page "F6/trx64"
    And the page has the button "Save" with the bkey S
    And the page "F6/video" has the button "Save" with the bkey S
    When I pack it
    Then packing succeeds

  Scenario: a key and a bkey of one page share the letters
    Given a menu description with the page "F5/drives"
    And the page has the button "Eject" with the bkey E
    And the page has the button "Extract" with the key E
    When I pack it
    Then packing is refused with "the letter E is claimed by"

  Scenario: a digit is not a letter
    Given a menu description with the page "F5/drives"
    And the page has the button "One" with the bkey 1
    When I pack it
    Then packing is refused with "one upper case letter"

  Scenario: a lower case letter is refused
    Given a menu description with the page "F5/drives"
    And the page has the button "Eject" with the bkey e
    When I pack it
    Then packing is refused with "one upper case letter"
