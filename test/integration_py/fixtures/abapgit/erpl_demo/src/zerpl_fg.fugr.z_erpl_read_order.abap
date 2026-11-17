FUNCTION z_erpl_read_order.
*"----------------------------------------------------------------------
*"*"Local Interface:
*"  IMPORTING
*"     VALUE(IV_SO_ID) TYPE  SNWD_SO_ID
*"  EXPORTING
*"     VALUE(ES_ORDER) TYPE  SNWD_SO
*"  TABLES
*"      ET_ITEMS STRUCTURE  SNWD_SO_I
*"  EXCEPTIONS
*"      NOT_FOUND
*"----------------------------------------------------------------------

  SELECT SINGLE * FROM snwd_so WHERE so_id = @iv_so_id INTO @es_order.
  IF sy-subrc <> 0.
    RAISE not_found.
  ENDIF.
  SELECT * FROM snwd_so_i WHERE parent_key = @es_order-node_key
    ORDER BY so_item_pos INTO TABLE @et_items.

ENDFUNCTION.
