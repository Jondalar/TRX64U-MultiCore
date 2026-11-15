# core: atari800
Feature: T189 the Tone row under Volume in the Audio group of the atari800 core (emulator pictures for the UX look)

  Scenario: F5 Audio page with Volume and Tone, then the Tone values
    Given the menu is open on F5 tab 3
    Then the page has the items "Volume"
    And the page has the items "Tone"
    And I save the picture "t189-atari800-audio"
    When I move to "Tone"
    Then I save the picture "t189-atari800-tone-selected"
    When I press cursor-right
    Then I save the picture "t189-atari800-tone-bass"
    When I press cursor-right
    Then I save the picture "t189-atari800-tone-treble"
