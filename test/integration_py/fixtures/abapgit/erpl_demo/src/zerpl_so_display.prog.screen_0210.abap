PROCESS BEFORE OUTPUT.
  LOOP AT gt_items INTO gs_item WITH CONTROL tc_items.
    MODULE fill_item_line.
  ENDLOOP.

PROCESS AFTER INPUT.
  LOOP AT gt_items.
    CHAIN.
      FIELD snwd_so_i-so_item_pos.
      FIELD snwd_pd-product_id.
      MODULE check_item_line ON CHAIN-REQUEST.
    ENDCHAIN.
  ENDLOOP.
