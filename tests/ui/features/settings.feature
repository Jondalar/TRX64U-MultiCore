Feature: Settings pages of the TRX64U menu: a change is applied and stored at once, reset-time items say when, Reset to host defaults (T153 S4-S5; T228 D9-D11: no hold, no Save bar, nothing written to flash)

  Background:
    Given the menu is open on F5 tab 1

  Scenario: the settings page has no Save, Discard or Defaults bar
    Then the page has no item "Save"
    And the page has no item "Discard"
    And the page has no item "Defaults"
    And the top bar does not show "UNSAVED"

  Scenario: a live setting is applied at once and nothing is held
    Then the setting "Drive enable" is "Enabled"
    When I move to "Drive enable"
    And I press RETURN
    Then the setting "Drive enable" is "Disabled"
    And the status line says "Drive enable"
    And the top bar does not show "UNSAVED"

  Scenario: a reset-time setting says that it takes effect at the next reset
    When I press 2
    And I set "Freeze mode" to "CPU only"
    Then the setting "Freeze mode" is "CPU only"
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
    When I move to "Drive enable"
    And I press RETURN
    And I press F8
    And I move to "Reset to host defaults"
    And I press RETURN
    Then a question is open: "Reset every setting to the host's defaults?"
    When I press RETURN
    Then no dialog is open
    And the status line says "Settings are the host's again"
    When I press F5
    Then the setting "Drive enable" is "Enabled"
