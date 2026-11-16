# core: pet
Feature: T216 missing-ROM message of the PET (run with UI_PET_DROP="edit-4-40-n-60Hz.901499-01.bin,kernal-4.901465-22.bin"; pictures only)

  Scenario: Two ROM files missing
    When I wait 6000 ms
    Then I save the picture "ux-pet-missing"
