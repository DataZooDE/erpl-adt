FUNCTION zagtest_function_module.
*"----------------------------------------------------------------------
*"*"Local Interface:
*"  IMPORTING
*"     REFERENCE(IMPORT_PARAMETER) TYPE  C
*"  EXPORTING
*"     REFERENCE(EXPORT_PARAMETER) TYPE  C
*"  TABLES
*"      TABLES_PARAMETER TYPE  STRING_TABLE
*"  CHANGING
*"     REFERENCE(CHANGING_PARAMETER) TYPE  C
*"----------------------------------------------------------------------

  WRITE: import_parameter, export_parameter, changing_parameter.

ENDFUNCTION.
