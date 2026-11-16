# core: pet
Feature: T232 pad keys A-Z and learn by key press on the PET Input tab (ux-review pictures; emulator pictures, nothing asserted on pixels)

  Scenario: pad rows, learn prompt, L, ESC
    Given the menu is open on F5 tab 4
    When I move to "Pad / joystick up"
    Then I save the picture "t232-pet-input-pad"
    When I press RETURN
    Then I save the picture "t232-pet-learn-prompt"
    When I press L
    Then the status line says "Key L"
    And I save the picture "t232-pet-after-l"
    When I press RETURN
    And I press RUN/STOP
    Then I save the picture "t232-pet-after-esc-menu-closed"
    Given the menu is open on F5 tab 4
    When I move to "Pad / joystick up"
    Then I save the picture "t232-pet-after-esc"
