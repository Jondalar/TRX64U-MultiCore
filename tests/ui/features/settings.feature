Feature: Settings pages: change, hold, Discard, Save, Defaults, with the letters of the buttons (T153 S4-S5)

  Background:
    Given the menu is open on F6 tab 1

  Scenario: the three buttons carry their letters
    Then the button "Discard" has the letter D
    And the button "Defaults" has the letter F
    And the button "Save" has the letter S

  Scenario: another settings page has the same bar
    When I press 2
    Then the tab is "Video"
    And the button "Discard" has the letter D
    And the button "Defaults" has the letter F
    And the button "Save" has the letter S

  Scenario: while Auto save config is Auto a change is written at once
    When I set "Auto save config" to "Auto"
    And I set "Overlay transparency" to "25 %"
    And I step "Overlay transparency" up 1 times
    Then the setting "Overlay transparency" is "50 %"
    And the top bar does not show "UNSAVED"
    And the button "Save" is greyed
    And the button "Discard" is greyed

  Scenario: with Auto save config Off a change is held and Discard takes it back
    When I set "Auto save config" to "Auto"
    And I set "Overlay transparency" to "25 %"
    And I set "Auto save config" to "Off"
    And I step "Overlay transparency" up 1 times
    Then the setting "Overlay transparency" is "50 %"
    And the top bar shows "UNSAVED"
    And the button "Discard" is enabled
    And the button "Save" is enabled
    When I press D
    Then the setting "Overlay transparency" is "25 %"

  Scenario: after Discard nothing is unsaved any more
    When I set "Auto save config" to "Auto"
    And I set "Overlay transparency" to "25 %"
    And I set "Auto save config" to "Off"
    And I step "Overlay transparency" up 1 times
    And I press D
    Then the top bar does not show "UNSAVED"
    And the button "Discard" is greyed
    And the button "Save" is greyed

  Scenario: Save writes the held change
    When I set "Auto save config" to "Auto"
    And I set "Overlay transparency" to "25 %"
    And I set "Auto save config" to "Off"
    And I step "Overlay transparency" up 1 times
    Then the top bar shows "UNSAVED"
    When I press S
    Then the status line says "Settings written"
    And the top bar does not show "UNSAVED"
    And the setting "Overlay transparency" is "50 %"
    And the button "Save" is greyed

  Scenario: Save with nothing to save only says why
    When I press S
    Then the status line says "Nothing to save"
    And the setting "Auto save config" is "Off"
    And the top bar does not show "UNSAVED"

  Scenario: Defaults asks first and the left arrow cancels
    When I set "Overlay transparency" to "75 %"
    And I press F
    Then a question is open: "Set every setting of this core to its default?"
    When I press left-arrow
    Then no dialog is open
    And the status line is "Cancelled"
    And the setting "Overlay transparency" is "75 %"

  Scenario: Defaults confirmed puts the defaults back
    When I set "Overlay transparency" to "75 %"
    And I press F
    And I press RETURN
    Then no dialog is open
    And the setting "Overlay transparency" is "25 %"
    And the setting "Auto save config" is "Auto"
