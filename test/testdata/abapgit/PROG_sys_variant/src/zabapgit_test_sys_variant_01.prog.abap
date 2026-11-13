*&---------------------------------------------------------------------*
*& Report zabapgit_test_sys_variant
*&---------------------------------------------------------------------*
*&
*&---------------------------------------------------------------------*
REPORT zabapgit_test_sys_variant_01.

PARAMETERS p_name TYPE c LENGTH 30 LOWER CASE.

START-OF-SELECTION.
  WRITE: 'Hello, ', p_name, /.
