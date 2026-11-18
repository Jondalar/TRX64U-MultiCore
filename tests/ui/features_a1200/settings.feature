# core: amiga1200
Feature: Settings of the Amiga 1200: a change is applied or stored at once, reset-time items say when, Reset to host defaults (T153 S4-S5 on the A1200, which has no hold and no Save bar)

  Background:
    Given the menu is open on F5 tab 1

  Scenario: the settings page has no Save, Discard or Defaults bar
    Then the page has no item "Save"
    And the page has no item "Discard"
    And the page has no item "Defaults"
    And the top bar does not show "UNSAVED"

  Scenario: a live setting is applied at once and nothing is held
    Then the setting "LED Load Indicator" is "On"
    When I step "LED Load Indicator" up 1 times
    Then the setting "LED Load Indicator" is "Off"
    And the status line says "LED Load Indicator"
    And the top bar does not show "UNSAVED"

  Scenario: a reset-time setting says that it takes effect at the next reset
    When I set "Chip RAM" to "1M"
    Then the setting "Chip RAM" is "1M"
    And the status line says "takes effect at the next reset"
    And the top bar does not show "UNSAVED"

  Scenario: a toggle on the settings page changes with RETURN
    When I press F7
    And I move to "Persist medium mounts"
    And I press RETURN
    Then the setting "Persist medium mounts" is "Yes"
    And the status line is "Persist medium mounts: Yes"
    When I press RETURN
    Then the setting "Persist medium mounts" is "No"

  Scenario: Reset to host defaults asks first, then puts the defaults back
    When I set "Chip RAM" to "1M"
    And I step "LED Load Indicator" up 1 times
    And I press F8
    And I move to "Reset to host defaults"
    And I press RETURN
    Then a question is open: "Reset every setting to the host's defaults?"
    When I press RETURN
    Then no dialog is open
    And the status line says "Settings are the host's again"
    When I press F5
    Then the setting "Chip RAM" is "2M"
    And the setting "LED Load Indicator" is "On"
