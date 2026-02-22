CLASS zcl_nw_shippers DEFINITION
  PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    INTERFACES if_oo_adt_classrun.
ENDCLASS.

CLASS zcl_nw_shippers IMPLEMENTATION.
  METHOD if_oo_adt_classrun~main.

    DATA lt_data TYPE TABLE OF znw_shippers WITH EMPTY KEY.

    lt_data = VALUE #(
      ( shipper_id = 1 company_name = 'Speedy Express'   phone = '(503) 555-9831' )
      ( shipper_id = 2 company_name = 'United Package'   phone = '(503) 555-3199' )
      ( shipper_id = 3 company_name = 'Federal Shipping' phone = '(503) 555-9931' )
    ).

    DELETE FROM znw_shippers.
    INSERT znw_shippers FROM TABLE @lt_data.

    out->write( |Loaded { lines( lt_data ) } Northwind shippers into ZNW_SHIPPERS| ).

  ENDMETHOD.
ENDCLASS.
