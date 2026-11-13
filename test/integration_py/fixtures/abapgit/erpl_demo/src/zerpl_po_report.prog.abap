*&---------------------------------------------------------------------*
*& Report ZERPL_PO_REPORT - Purchase orders by supplier
*&---------------------------------------------------------------------*
*& An ME2L-style list on the EPM demo model (SNWD_PO / SNWD_PO_I /
*& SNWD_BPA), shown as ALV grid (CL_SALV_TABLE).
*& Started by report transaction ZERPL_ME2L.
*&---------------------------------------------------------------------*
REPORT zerpl_po_report.

TYPES: BEGIN OF ty_line,
         bp_id          TYPE snwd_bpa-bp_id,
         company_name   TYPE snwd_bpa-company_name,
         po_id          TYPE snwd_po-po_id,
         po_item_pos    TYPE snwd_po_i-po_item_pos,
         gross_amount   TYPE snwd_po_i-gross_amount,
         currency_code  TYPE snwd_po_i-currency_code,
         lifecycle      TYPE snwd_po-lifecycle_status,
       END OF ty_line.

DATA: gt_lines  TYPE STANDARD TABLE OF ty_line WITH EMPTY KEY,
      gr_status TYPE RANGE OF snwd_po-lifecycle_status,
      gv_bp_id  TYPE snwd_bpa-bp_id,
      gv_po_id  TYPE snwd_po-po_id.

SELECTION-SCREEN BEGIN OF BLOCK sel WITH FRAME TITLE TEXT-b01.
  SELECT-OPTIONS: s_bp_id FOR gv_bp_id,
                  s_po_id FOR gv_po_id.
  PARAMETERS: p_status TYPE snwd_po-lifecycle_status,
              p_max    TYPE i DEFAULT 1000.
SELECTION-SCREEN END OF BLOCK sel.

START-OF-SELECTION.
  IF p_status IS NOT INITIAL.
    gr_status = VALUE #( ( sign = 'I' option = 'EQ' low = p_status ) ).
  ENDIF.

  SELECT b~bp_id, b~company_name, h~po_id, i~po_item_pos,
         i~gross_amount, i~currency_code, h~lifecycle_status AS lifecycle
    FROM snwd_po AS h
    INNER JOIN snwd_po_i AS i ON i~parent_key = h~node_key
    INNER JOIN snwd_bpa AS b ON b~node_key = h~partner_guid
    WHERE b~bp_id IN @s_bp_id
      AND h~po_id IN @s_po_id
      AND h~lifecycle_status IN @gr_status
    ORDER BY b~bp_id, h~po_id, i~po_item_pos
    INTO CORRESPONDING FIELDS OF TABLE @gt_lines
    UP TO @p_max ROWS.

  IF gt_lines IS INITIAL.
    MESSAGE s398(00) WITH TEXT-001 DISPLAY LIKE 'W'.
    RETURN.
  ENDIF.

  TRY.
      cl_salv_table=>factory( IMPORTING r_salv_table = DATA(lo_alv)
                              CHANGING  t_table      = gt_lines ).
      lo_alv->get_functions( )->set_all( abap_true ).
      lo_alv->get_columns( )->set_optimize( abap_true ).
      lo_alv->get_display_settings( )->set_list_header( CONV #( TEXT-002 ) ).
      lo_alv->get_sorts( )->add_sort( columnname = 'BP_ID' subtotal = abap_true ).
      lo_alv->get_aggregations( )->add_aggregation( columnname = 'GROSS_AMOUNT' ).
      lo_alv->display( ).
    CATCH cx_salv_error INTO DATA(lx_salv).
      MESSAGE lx_salv TYPE 'I' DISPLAY LIKE 'E'.
  ENDTRY.
