PROCESS BEFORE OUTPUT.
  MODULE set_sash.
  CALL SUBSCREEN subscreen1
       INCLUDING sy-repid '110'.
  CALL SUBSCREEN subscreen2
       INCLUDING sy-repid '120'.

PROCESS AFTER INPUT.
  MODULE get_sash.
  CALL SUBSCREEN subscreen1.
  CALL SUBSCREEN subscreen2.
