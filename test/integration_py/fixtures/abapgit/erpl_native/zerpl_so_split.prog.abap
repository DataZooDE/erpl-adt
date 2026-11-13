*&---------------------------------------------------------------------*
*& Report ZERPL_SO_SPLIT - Sales order cockpit
*&---------------------------------------------------------------------*
*& Screen 0100 is a dynpro splitter control (CL_DYNPRO_SPLITTER): the
*& order list (subscreen 0110) on the left, its items (0120) on the
*& right. abapGit stores a splitter screen in SAP's native dynpro format
*& (NAT_HEADER / NAT_FIELDS in the .prog.xml).
*&---------------------------------------------------------------------*
REPORT zerpl_so_split.

DATA splitter TYPE REF TO cl_dynpro_splitter.

START-OF-SELECTION.
  CREATE OBJECT splitter
    EXPORTING
      splitter_name = 'SPLITTER'.
  CALL SCREEN 100.

MODULE set_status OUTPUT.
  SET PF-STATUS 'SCREEN_100'.
ENDMODULE.

MODULE set_sash OUTPUT.
  splitter->set_sash( ).
ENDMODULE.

MODULE cancel INPUT.
  LEAVE PROGRAM.
ENDMODULE.

MODULE get_sash INPUT.
  splitter->get_guisash( ).
ENDMODULE.
