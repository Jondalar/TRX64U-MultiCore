# core: amiga1200
Feature: Settings of the Amiga 1200: a change is applied or stored at once, reset-time items say when, Reset to host defaults (T153 S4-S5 on the A1200, which has no hold and no Save bar)

  Background:
    Given the menu is open on F5 tab 2

  Scenario: the settings page has no Save, Discard or Defaults bar
    Then the page has no item "Save"
    And the page has no item "Discard"
    And the page has no item "Defaults"
    And the top bar does not show "UNSAVED"

  Scenario: a live setting is applied at once and nothing is held
    Then the setting "Mouse sensitivity" is "8"
    When I step "Mouse sensitivity" up 1 times
    Then the setting "Mouse sensitivity" is "9"
    And the status line says "Mouse sensitivity"
    And the top bar does not show "UNSAVED"

  Scenario: a reset-time setting says that it takes effect at the next reset
    When I set "Chip RAM" to "1M"
    Then the setting "Chip RAM" is "1M"
    And the status line says "takes effect at the next reset"
    And the top bar does not show "UNSAVED"

  Scenario: a toggle on another page changes with RETURN
    When I press F8
    And I move to "Persist medium mounts"
    And I press RETURN
    Then the setting "Persist medium mounts" is "Yes"
    And the status line is "Persist medium mounts: Yes"
    When I press RETURN
    Then the setting "Persist medium mounts" is "No"

  Scenario: Reset to host defaults puts the defaults back and does not ask
    When I set "Chip RAM" to "1M"
    And I step "Mouse sensitivity" up 2 times
    And I press F8
    And I move to "Reset to host defaults"
    And I press RETURN
    Then no dialog is open
    And the status line says "Settings are the host's again"
    When I press F5
    Then the setting "Chip RAM" is "2M"
    And the setting "Mouse sensitivity" is "8"
