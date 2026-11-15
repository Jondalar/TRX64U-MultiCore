# core: neogeo
Feature: T189 neogeo has no tone filter in its build (D6), so its Audio page shows no Tone row (emulator picture)

  Scenario: F5 Audio page
    Given the menu is open on F5 tab 3
    Then the page has no item "Tone"
    And I save the picture "t189-neogeo-audio"
