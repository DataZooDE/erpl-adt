REPORT zdemo_splitter_subscreen.

DATA splitter TYPE REF TO cl_dynpro_splitter.

START-OF-SELECTION.

  CREATE OBJECT splitter
    EXPORTING
      splitter_name = 'SPLITTER'.

  CALL SCREEN 200.

MODULE set_status OUTPUT.
  SET PF-STATUS 'SCREEN_100'.
ENDMODULE.

MODULE set_sash OUTPUT.
  splitter->set_sash( ).
ENDMODULE.

MODULE cancel.
  LEAVE PROGRAM.
ENDMODULE.

MODULE get_sash INPUT.
  splitter->get_guisash( ).
ENDMODULE.
