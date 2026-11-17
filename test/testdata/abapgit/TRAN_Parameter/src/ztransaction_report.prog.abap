*&---------------------------------------------------------------------*
*& Report ZTRANSACTION_REPORT
*&---------------------------------------------------------------------*
REPORT ztransaction_report.

TABLES: t100.

SELECT-OPTIONS s_arbgb FOR t100-arbgb.
PARAMETERS p_sprsl TYPE t100-sprsl.

START-OF-SELECTION.
