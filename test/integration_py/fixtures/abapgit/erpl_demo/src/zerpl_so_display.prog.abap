*&---------------------------------------------------------------------*
*& Module pool ZERPL_SO_DISPLAY - Display Sales Order
*&---------------------------------------------------------------------*
*& A VA03-style display transaction on the EPM demo model:
*&   0100  initial screen: sales order number (F4 help), Display
*&   0200  overview: header data, tabstrip with items / notes
*&   0210  subscreen: items in table control TC_ITEMS
*&   0220  subscreen: notes in a text edit control
*&   0300  dialog box: order info
*& Started by dialog transaction ZERPL_VA03 at screen 0100.
*&---------------------------------------------------------------------*
PROGRAM zerpl_so_display.

TABLES: snwd_so, snwd_so_i, snwd_pd.

" SNWD_BPA is not flat (WEB_ADDRESS is a string), so no TABLES work area:
" the screen field SNWD_BPA-COMPANY_NAME is fed from this structure.
DATA: BEGIN OF snwd_bpa,
        bp_id        TYPE snwd_bpa-bp_id,
        company_name TYPE snwd_bpa-company_name,
      END OF snwd_bpa.

CONTROLS: ts_detail TYPE TABSTRIP,
          tc_items  TYPE TABLEVIEW USING SCREEN 0210.

TYPES: BEGIN OF ty_item,
         so_item_pos   TYPE snwd_so_i-so_item_pos,
         product_id    TYPE snwd_pd-product_id,
         net_amount    TYPE snwd_so_i-net_amount,
         currency_code TYPE snwd_so_i-currency_code,
       END OF ty_item,
       BEGIN OF ty_f4,
         so_id         TYPE snwd_so-so_id,
         gross_amount  TYPE snwd_so-gross_amount,
         currency_code TYPE snwd_so-currency_code,
       END OF ty_f4.

DATA: ok_code      TYPE sy-ucomm,
      save_ok      TYPE sy-ucomm,
      gt_items     TYPE STANDARD TABLE OF ty_item WITH EMPTY KEY,
      gs_item      TYPE ty_item,
      gt_f4        TYPE STANDARD TABLE OF ty_f4 WITH EMPTY KEY,
      gv_billed    TYPE c LENGTH 1,
      gv_net       TYPE c LENGTH 1 VALUE 'X',
      gv_gross     TYPE c LENGTH 1,
      gv_found     TYPE abap_bool,
      gv_notes     TYPE string,
      go_container TYPE REF TO cl_gui_custom_container,
      go_notes     TYPE REF TO cl_gui_textedit.

*----------------------------------------------------------------------*
* Screen 0100 - initial screen
*----------------------------------------------------------------------*
MODULE status_0100 OUTPUT.
  SET PF-STATUS 'MAIN' EXCLUDING 'REFRESH'.
  SET TITLEBAR 'T0100'.
ENDMODULE.

MODULE exit_command INPUT.
  CASE ok_code.
    WHEN 'EXIT'.
      LEAVE PROGRAM.
    WHEN OTHERS.
      IF sy-dynnr = '0100'.
        LEAVE PROGRAM.
      ENDIF.
      CLEAR ok_code.
      LEAVE TO SCREEN 0100.
  ENDCASE.
ENDMODULE.

MODULE check_so_id INPUT.
  PERFORM read_order.
  IF gv_found = abap_false.
    MESSAGE e398(00) WITH TEXT-001 snwd_so-so_id.
  ENDIF.
ENDMODULE.

MODULE f4_so_id INPUT.
  SELECT so_id, gross_amount, currency_code
    FROM snwd_so
    ORDER BY so_id
    INTO CORRESPONDING FIELDS OF TABLE @gt_f4
    UP TO 500 ROWS.
  CALL FUNCTION 'F4IF_INT_TABLE_VALUE_REQUEST'
    EXPORTING
      retfield    = 'SO_ID'
      dynpprog    = sy-repid
      dynpnr      = sy-dynnr
      dynprofield = 'SNWD_SO-SO_ID'
      value_org   = 'S'
    TABLES
      value_tab   = gt_f4
    EXCEPTIONS
      OTHERS      = 1.
  IF sy-subrc <> 0.
    MESSAGE s398(00) WITH TEXT-002 DISPLAY LIKE 'E'.
  ENDIF.
ENDMODULE.

MODULE user_command_0100 INPUT.
  save_ok = ok_code.
  CLEAR ok_code.
  CASE save_ok.
    WHEN 'DISP' OR space.
      LEAVE TO SCREEN 0200.
    WHEN 'INFO'.
      CALL SCREEN 0300 STARTING AT 10 5 ENDING AT 60 10.
  ENDCASE.
ENDMODULE.

*----------------------------------------------------------------------*
* Screen 0200 - overview with tabstrip
*----------------------------------------------------------------------*
MODULE status_0200 OUTPUT.
  SET PF-STATUS 'MAIN'.
  SET TITLEBAR 'T0200' WITH snwd_so-so_id.
  IF ts_detail-activetab IS INITIAL.
    ts_detail-activetab = 'TAB_ITEMS'.
  ENDIF.
ENDMODULE.

MODULE user_command_0200 INPUT.
  save_ok = ok_code.
  CLEAR ok_code.
  CASE save_ok.
    WHEN 'REFRESH'.
      PERFORM read_order.
    WHEN 'INFO'.
      CALL SCREEN 0300 STARTING AT 10 5 ENDING AT 60 10.
    WHEN 'OTHER'.
      LEAVE TO SCREEN 0100.
  ENDCASE.
ENDMODULE.

*----------------------------------------------------------------------*
* Screen 0210 - items (table control)
*----------------------------------------------------------------------*
MODULE fill_item_line OUTPUT.
  snwd_so_i-so_item_pos   = gs_item-so_item_pos.
  snwd_pd-product_id      = gs_item-product_id.
  snwd_so_i-net_amount    = gs_item-net_amount.
  snwd_so_i-currency_code = gs_item-currency_code.
ENDMODULE.

MODULE check_item_line INPUT.
  gs_item-so_item_pos = snwd_so_i-so_item_pos.
  gs_item-product_id  = snwd_pd-product_id.
  MODIFY gt_items FROM gs_item INDEX tc_items-current_line
    TRANSPORTING so_item_pos product_id.
ENDMODULE.

*----------------------------------------------------------------------*
* Screen 0220 - notes (custom control)
*----------------------------------------------------------------------*
MODULE show_notes OUTPUT.
  IF go_container IS INITIAL.
    CREATE OBJECT go_container
      EXPORTING
        container_name = 'NOTES_CONT'
      EXCEPTIONS
        OTHERS         = 1.
    IF sy-subrc <> 0.
      RETURN.
    ENDIF.
    CREATE OBJECT go_notes
      EXPORTING
        parent = go_container
      EXCEPTIONS
        OTHERS = 1.
    IF sy-subrc <> 0.
      RETURN.
    ENDIF.
    go_notes->set_readonly_mode( EXPORTING readonly_mode = 1 EXCEPTIONS OTHERS = 1 ).
  ENDIF.
  gv_notes = |{ TEXT-003 }: { snwd_bpa-company_name }| &&
              cl_abap_char_utilities=>newline &&
              |{ TEXT-004 }: { lines( gt_items ) }|.
  go_notes->set_textstream( EXPORTING text = gv_notes EXCEPTIONS OTHERS = 1 ).
ENDMODULE.

*----------------------------------------------------------------------*
* Screen 0300 - order info (dialog box)
*----------------------------------------------------------------------*
MODULE status_0300 OUTPUT.
  SET PF-STATUS 'DIALOG'.
ENDMODULE.

MODULE user_command_0300 INPUT.
  CLEAR ok_code.
  LEAVE TO SCREEN 0.
ENDMODULE.

*----------------------------------------------------------------------*
FORM read_order.
  DATA lv_so_id TYPE snwd_so-so_id.

  gv_found = abap_false.
  lv_so_id = |{ snwd_so-so_id ALPHA = IN }|.
  SELECT SINGLE * FROM snwd_so WHERE so_id = @lv_so_id INTO @snwd_so.
  IF sy-subrc <> 0.
    CLEAR: snwd_bpa, gt_items.
    RETURN.
  ENDIF.
  gv_found = abap_true.

  SELECT SINGLE bp_id, company_name FROM snwd_bpa
    WHERE node_key = @snwd_so-buyer_guid
    INTO CORRESPONDING FIELDS OF @snwd_bpa.

  SELECT i~so_item_pos, p~product_id, i~net_amount, i~currency_code
    FROM snwd_so_i AS i
    LEFT OUTER JOIN snwd_pd AS p ON p~node_key = i~product_guid
    WHERE i~parent_key = @snwd_so-node_key
    ORDER BY i~so_item_pos
    INTO CORRESPONDING FIELDS OF TABLE @gt_items.
  tc_items-lines = lines( gt_items ).

  gv_billed = xsdbool( snwd_so-billing_status <> space ).
ENDFORM.
