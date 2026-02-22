CLASS zcl_nw_categories DEFINITION
  PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    INTERFACES if_oo_adt_classrun.
ENDCLASS.

CLASS zcl_nw_categories IMPLEMENTATION.
  METHOD if_oo_adt_classrun~main.

    DATA lt_data TYPE TABLE OF znw_categories WITH EMPTY KEY.

    lt_data = VALUE #(
      ( category_id = 1 category_name = 'Beverages'    description = 'Soft drinks, coffees, teas, beers, and ales' )
      ( category_id = 2 category_name = 'Condiments'   description = 'Sweet and savory sauces, relishes, spreads, and seasonings' )
      ( category_id = 3 category_name = 'Confections'  description = 'Desserts, candies, and sweet breads' )
      ( category_id = 4 category_name = 'Dairy Products' description = 'Cheeses' )
      ( category_id = 5 category_name = 'Grains/Cereals' description = 'Breads, crackers, pasta, and cereal' )
      ( category_id = 6 category_name = 'Meat/Poultry' description = 'Prepared meats' )
      ( category_id = 7 category_name = 'Produce'      description = 'Dried fruit and bean curd' )
      ( category_id = 8 category_name = 'Seafood'      description = 'Seaweed and fish' )
    ).

    DELETE FROM znw_categories.
    INSERT znw_categories FROM TABLE @lt_data.

    out->write( |Loaded { lines( lt_data ) } Northwind categories into ZNW_CATEGORIES| ).

  ENDMETHOD.
ENDCLASS.
