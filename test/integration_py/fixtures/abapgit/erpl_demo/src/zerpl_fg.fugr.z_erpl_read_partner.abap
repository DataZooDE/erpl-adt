FUNCTION z_erpl_read_partner.
*"----------------------------------------------------------------------
*"*"Local Interface:
*"  IMPORTING
*"     VALUE(IV_BP_ID) TYPE  SNWD_PARTNER_ID
*"     REFERENCE(IV_WITH_CONTACTS) TYPE  ABAP_BOOL DEFAULT ABAP_FALSE
*"     REFERENCE(IV_LANGU) LIKE  SY-LANGU OPTIONAL
*"  EXPORTING
*"     VALUE(ES_PARTNER) TYPE  SNWD_BPA
*"  CHANGING
*"     REFERENCE(CT_LOG) TYPE  STRING_TABLE OPTIONAL
*"  RAISING
*"      CX_SY_ITAB_LINE_NOT_FOUND
*"----------------------------------------------------------------------
  " Reads one business partner of the EPM demo model.
  SELECT SINGLE * FROM snwd_bpa WHERE bp_id = @iv_bp_id INTO @es_partner.
  IF sy-subrc <> 0.
    RAISE EXCEPTION TYPE cx_sy_itab_line_not_found.
  ENDIF.
  APPEND |Partner { iv_bp_id } read (contacts: { iv_with_contacts }, { iv_langu })| TO ct_log.
ENDFUNCTION.
