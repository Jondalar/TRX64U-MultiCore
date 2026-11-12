# core: pet
Feature: The PET core app after the heap-buffer change of 07713664b (T210: ROM put, disk mount and PRG load through the Scratch buffers; ue2emu profile, no PET machine behind it)

  Scenario: the menu, F1 and F5
    Given the menu is open
    Then I save the picture "t210-pet-f1-home"
    When I press F5
    Then I save the picture "t210-pet-f5"

  Scenario: a D64 is mounted from the Library
    Given the menu is open on F3
    And I go up to the top of the medium "USB0"
    When I move to the file "games"
    And I press RETURN
    And I move to the file "alpha.d64"
    And I press RETURN
    Then the sheet lists "Mount in Drive 8"
    When I press RETURN
    Then the status line says "alpha"

  Scenario: a PRG is loaded from the Library
    Given the menu is open on F3
    And I go up to the top of the medium "USB0"
    When I move to the file "games"
    And I press RETURN
    And I move to the file "hello.prg"
    And I press RETURN
    Then the sheet lists "Load"
    When I press RETURN
    Then the status line says "hello"
