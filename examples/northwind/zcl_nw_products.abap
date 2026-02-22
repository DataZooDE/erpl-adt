CLASS zcl_nw_products DEFINITION
  PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    INTERFACES if_oo_adt_classrun.
ENDCLASS.

CLASS zcl_nw_products IMPLEMENTATION.
  METHOD if_oo_adt_classrun~main.

    TYPES: BEGIN OF ty_product,
             product_id    TYPE i,
             product_name  TYPE char40,
             supplier_id   TYPE i,
             category_id   TYPE i,
             qty_per_unit  TYPE char20,
             unit_price    TYPE p LENGTH 8 DECIMALS 2,
             units_stock   TYPE i,
             units_order   TYPE i,
             reorder_level TYPE i,
             discontinued  TYPE char1,
           END OF ty_product.

    DATA lt_products TYPE TABLE OF ty_product.

    lt_products = VALUE #(
      ( product_id = 1   product_name = 'Chai'                              supplier_id = 1  category_id = 1 qty_per_unit = '10 boxes x 20 bags'   unit_price = '18.00' units_stock = 39  units_order = 0  reorder_level = 10 discontinued = '' )
      ( product_id = 2   product_name = 'Chang'                             supplier_id = 1  category_id = 1 qty_per_unit = '24 - 12 oz bottles'   unit_price = '19.00' units_stock = 17  units_order = 40 reorder_level = 25 discontinued = '' )
      ( product_id = 3   product_name = 'Aniseed Syrup'                     supplier_id = 1  category_id = 2 qty_per_unit = '12 - 550 ml bottles'   unit_price = '10.00' units_stock = 13  units_order = 70 reorder_level = 25 discontinued = '' )
      ( product_id = 4   product_name = 'Chef Anton''s Cajun Seasoning'     supplier_id = 2  category_id = 2 qty_per_unit = '48 - 6 oz jars'        unit_price = '22.00' units_stock = 53  units_order = 0  reorder_level = 0  discontinued = '' )
      ( product_id = 5   product_name = 'Chef Anton''s Gumbo Mix'           supplier_id = 2  category_id = 2 qty_per_unit = '36 boxes'               unit_price = '21.35' units_stock = 0   units_order = 0  reorder_level = 0  discontinued = 'X' )
      ( product_id = 6   product_name = 'Grandma''s Boysenberry Spread'     supplier_id = 3  category_id = 2 qty_per_unit = '12 - 8 oz jars'         unit_price = '25.00' units_stock = 120 units_order = 0  reorder_level = 25 discontinued = '' )
      ( product_id = 7   product_name = 'Uncle Bob''s Organic Dried Pears'  supplier_id = 3  category_id = 7 qty_per_unit = '12 - 1 lb pkgs.'        unit_price = '30.00' units_stock = 15  units_order = 0  reorder_level = 10 discontinued = '' )
      ( product_id = 8   product_name = 'Northwoods Cranberry Sauce'        supplier_id = 3  category_id = 2 qty_per_unit = '12 - 12 oz jars'        unit_price = '40.00' units_stock = 6   units_order = 0  reorder_level = 0  discontinued = '' )
      ( product_id = 9   product_name = 'Mishi Kobe Niku'                   supplier_id = 4  category_id = 6 qty_per_unit = '18 - 500 g pkgs.'       unit_price = '97.00' units_stock = 29  units_order = 0  reorder_level = 0  discontinued = 'X' )
      ( product_id = 10  product_name = 'Ikura'                             supplier_id = 4  category_id = 8 qty_per_unit = '12 - 200 ml jars'       unit_price = '31.00' units_stock = 31  units_order = 0  reorder_level = 0  discontinued = '' )
      ( product_id = 11  product_name = 'Queso Cabrales'                    supplier_id = 5  category_id = 4 qty_per_unit = '1 kg pkg.'               unit_price = '21.00' units_stock = 22  units_order = 30 reorder_level = 30 discontinued = '' )
      ( product_id = 12  product_name = 'Queso Manchego La Pastora'         supplier_id = 5  category_id = 4 qty_per_unit = '10 - 500 g pkgs.'       unit_price = '38.00' units_stock = 86  units_order = 0  reorder_level = 0  discontinued = '' )
      ( product_id = 13  product_name = 'Konbu'                             supplier_id = 6  category_id = 8 qty_per_unit = '2 kg box'                unit_price = '6.00'  units_stock = 24  units_order = 0  reorder_level = 5  discontinued = '' )
      ( product_id = 14  product_name = 'Tofu'                              supplier_id = 6  category_id = 7 qty_per_unit = '40 - 100 g pkgs.'       unit_price = '23.25' units_stock = 35  units_order = 0  reorder_level = 0  discontinued = '' )
      ( product_id = 15  product_name = 'Genen Shouyu'                      supplier_id = 6  category_id = 2 qty_per_unit = '24 - 250 ml bottles'    unit_price = '15.50' units_stock = 39  units_order = 0  reorder_level = 5  discontinued = '' )
      ( product_id = 16  product_name = 'Pavlova'                           supplier_id = 7  category_id = 3 qty_per_unit = '32 - 500 g boxes'       unit_price = '17.45' units_stock = 29  units_order = 0  reorder_level = 10 discontinued = '' )
      ( product_id = 17  product_name = 'Alice Mutton'                      supplier_id = 7  category_id = 6 qty_per_unit = '20 - 1 kg tins'         unit_price = '39.00' units_stock = 0   units_order = 0  reorder_level = 0  discontinued = 'X' )
      ( product_id = 18  product_name = 'Carnarvon Tigers'                  supplier_id = 7  category_id = 8 qty_per_unit = '16 kg pkg.'              unit_price = '62.50' units_stock = 42  units_order = 0  reorder_level = 0  discontinued = '' )
      ( product_id = 19  product_name = 'Teatime Chocolate Biscuits'        supplier_id = 8  category_id = 3 qty_per_unit = '10 boxes x 12 pieces'   unit_price = '9.20'  units_stock = 25  units_order = 0  reorder_level = 5  discontinued = '' )
      ( product_id = 20  product_name = 'Sir Rodney''s Marmalade'           supplier_id = 8  category_id = 3 qty_per_unit = '30 gift boxes'           unit_price = '81.00' units_stock = 40  units_order = 0  reorder_level = 0  discontinued = '' )
      ( product_id = 21  product_name = 'Sir Rodney''s Scones'              supplier_id = 8  category_id = 3 qty_per_unit = '24 pkgs. x 4 pieces'    unit_price = '10.00' units_stock = 3   units_order = 40 reorder_level = 5  discontinued = '' )
      ( product_id = 22  product_name = 'Gustaf''s Knackebrod'              supplier_id = 9  category_id = 5 qty_per_unit = '24 - 500 g pkgs.'       unit_price = '21.00' units_stock = 104 units_order = 0  reorder_level = 25 discontinued = '' )
      ( product_id = 23  product_name = 'Tunnbrod'                          supplier_id = 9  category_id = 5 qty_per_unit = '12 - 250 g pkgs.'       unit_price = '9.00'  units_stock = 61  units_order = 0  reorder_level = 25 discontinued = '' )
      ( product_id = 24  product_name = 'Guaran Fantastica'                 supplier_id = 10 category_id = 1 qty_per_unit = '12 - 355 ml cans'       unit_price = '4.50'  units_stock = 20  units_order = 0  reorder_level = 0  discontinued = 'X' )
      ( product_id = 25  product_name = 'NuNuCa Nuss-Nougat-Creme'         supplier_id = 11 category_id = 3 qty_per_unit = '20 - 450 g glasses'     unit_price = '14.00' units_stock = 76  units_order = 0  reorder_level = 30 discontinued = '' )
      ( product_id = 26  product_name = 'Gumbar Gummibarchen'              supplier_id = 11 category_id = 3 qty_per_unit = '100 - 250 g bags'        unit_price = '31.23' units_stock = 15  units_order = 0  reorder_level = 0  discontinued = '' )
      ( product_id = 27  product_name = 'Schoggi Schokolade'                supplier_id = 11 category_id = 3 qty_per_unit = '100 - 100 g pieces'     unit_price = '43.90' units_stock = 49  units_order = 0  reorder_level = 30 discontinued = '' )
      ( product_id = 28  product_name = 'Rossle Sauerkraut'                 supplier_id = 12 category_id = 7 qty_per_unit = '25 - 825 g cans'        unit_price = '45.60' units_stock = 26  units_order = 0  reorder_level = 0  discontinued = 'X' )
      ( product_id = 29  product_name = 'Thuringer Rostbratwurst'           supplier_id = 12 category_id = 6 qty_per_unit = '50 bags x 30 sausgs.'   unit_price = '123.79' units_stock = 0  units_order = 0  reorder_level = 0  discontinued = 'X' )
      ( product_id = 30  product_name = 'Nord-Ost Matjeshering'             supplier_id = 13 category_id = 8 qty_per_unit = '10 - 200 g glasses'     unit_price = '25.89' units_stock = 10  units_order = 0  reorder_level = 15 discontinued = '' )
      ( product_id = 31  product_name = 'Gorgonzola Telino'                 supplier_id = 14 category_id = 4 qty_per_unit = '12 - 100 g pkgs'        unit_price = '12.50' units_stock = 0   units_order = 70 reorder_level = 20 discontinued = '' )
      ( product_id = 32  product_name = 'Mascarpone Fabioli'                supplier_id = 14 category_id = 4 qty_per_unit = '24 - 200 g pkgs.'       unit_price = '32.00' units_stock = 9   units_order = 40 reorder_level = 25 discontinued = '' )
      ( product_id = 33  product_name = 'Geitost'                           supplier_id = 15 category_id = 4 qty_per_unit = '500 g'                  unit_price = '2.50'  units_stock = 112 units_order = 0  reorder_level = 20 discontinued = '' )
      ( product_id = 34  product_name = 'Sasquatch Ale'                     supplier_id = 16 category_id = 1 qty_per_unit = '24 - 12 oz bottles'    unit_price = '14.00' units_stock = 111 units_order = 0  reorder_level = 15 discontinued = '' )
      ( product_id = 35  product_name = 'Steeleye Stout'                    supplier_id = 16 category_id = 1 qty_per_unit = '24 - 12 oz bottles'    unit_price = '18.00' units_stock = 20  units_order = 0  reorder_level = 15 discontinued = '' )
      ( product_id = 36  product_name = 'Inlagd Sill'                       supplier_id = 17 category_id = 8 qty_per_unit = '24 - 250 g jars'       unit_price = '19.00' units_stock = 112 units_order = 0  reorder_level = 20 discontinued = '' )
      ( product_id = 37  product_name = 'Gravad lax'                        supplier_id = 17 category_id = 8 qty_per_unit = '12 - 500 g pkgs.'       unit_price = '26.00' units_stock = 11  units_order = 50 reorder_level = 25 discontinued = '' )
      ( product_id = 38  product_name = 'Cote de Blaye'                     supplier_id = 18 category_id = 1 qty_per_unit = '12 - 75 cl bottles'    unit_price = '263.50' units_stock = 17 units_order = 0  reorder_level = 15 discontinued = '' )
      ( product_id = 39  product_name = 'Chartreuse verte'                  supplier_id = 18 category_id = 1 qty_per_unit = '750 cc per bottle'      unit_price = '18.00' units_stock = 69  units_order = 0  reorder_level = 5  discontinued = '' )
      ( product_id = 40  product_name = 'Boston Crab Meat'                  supplier_id = 19 category_id = 8 qty_per_unit = '24 - 4 oz tins'        unit_price = '18.40' units_stock = 123 units_order = 0  reorder_level = 30 discontinued = '' )
      ( product_id = 41  product_name = 'Jack''s New England Clam Chowder'  supplier_id = 19 category_id = 8 qty_per_unit = '12 - 12 oz cans'       unit_price = '9.65'  units_stock = 85  units_order = 0  reorder_level = 10 discontinued = '' )
      ( product_id = 42  product_name = 'Singaporean Hokkien Fried Mee'    supplier_id = 20 category_id = 5 qty_per_unit = '32 - 1 kg pkgs.'        unit_price = '14.00' units_stock = 26  units_order = 0  reorder_level = 0  discontinued = 'X' )
      ( product_id = 43  product_name = 'Ipoh Coffee'                       supplier_id = 20 category_id = 1 qty_per_unit = '16 - 500 g tins'       unit_price = '46.00' units_stock = 17  units_order = 10 reorder_level = 25 discontinued = '' )
      ( product_id = 44  product_name = 'Gula Malacca'                      supplier_id = 20 category_id = 2 qty_per_unit = '20 - 2 kg bags'        unit_price = '19.45' units_stock = 27  units_order = 0  reorder_level = 15 discontinued = '' )
      ( product_id = 45  product_name = 'Rogede sild'                       supplier_id = 21 category_id = 8 qty_per_unit = '1k pkg.'                unit_price = '9.50'  units_stock = 5   units_order = 70 reorder_level = 15 discontinued = '' )
      ( product_id = 46  product_name = 'Spegesild'                         supplier_id = 21 category_id = 8 qty_per_unit = '4 - 450 g glasses'     unit_price = '12.00' units_stock = 95  units_order = 0  reorder_level = 0  discontinued = '' )
      ( product_id = 47  product_name = 'Zaanse koeken'                     supplier_id = 22 category_id = 3 qty_per_unit = '10 - 4 oz boxes'       unit_price = '9.50'  units_stock = 36  units_order = 0  reorder_level = 0  discontinued = '' )
      ( product_id = 48  product_name = 'Chocolade'                         supplier_id = 22 category_id = 3 qty_per_unit = '10 pkgs.'               unit_price = '12.75' units_stock = 15  units_order = 70 reorder_level = 25 discontinued = '' )
      ( product_id = 49  product_name = 'Maxilaku'                          supplier_id = 23 category_id = 3 qty_per_unit = '24 - 50 g pkgs.'       unit_price = '20.00' units_stock = 10  units_order = 60 reorder_level = 15 discontinued = '' )
      ( product_id = 50  product_name = 'Valkoinen suklaa'                  supplier_id = 23 category_id = 3 qty_per_unit = '12 - 100 g bars'       unit_price = '16.25' units_stock = 65  units_order = 0  reorder_level = 30 discontinued = '' )
      ( product_id = 51  product_name = 'Manjimup Dried Apples'             supplier_id = 24 category_id = 7 qty_per_unit = '50 - 300 g pkgs.'      unit_price = '53.00' units_stock = 20  units_order = 0  reorder_level = 10 discontinued = '' )
      ( product_id = 52  product_name = 'Filo Mix'                          supplier_id = 24 category_id = 5 qty_per_unit = '16 - 2 kg boxes'       unit_price = '7.00'  units_stock = 38  units_order = 0  reorder_level = 25 discontinued = '' )
      ( product_id = 53  product_name = 'Perth Pasties'                     supplier_id = 24 category_id = 6 qty_per_unit = '48 pieces'              unit_price = '32.80' units_stock = 0   units_order = 0  reorder_level = 0  discontinued = 'X' )
      ( product_id = 54  product_name = 'Tourtiere'                         supplier_id = 25 category_id = 6 qty_per_unit = '16 pies'               unit_price = '7.45'  units_stock = 21  units_order = 0  reorder_level = 10 discontinued = '' )
      ( product_id = 55  product_name = 'Pate chinois'                      supplier_id = 25 category_id = 6 qty_per_unit = '24 boxes x 2 pies'     unit_price = '24.00' units_stock = 115 units_order = 0  reorder_level = 20 discontinued = '' )
      ( product_id = 56  product_name = 'Gnocchi di nonna Alice'            supplier_id = 26 category_id = 5 qty_per_unit = '24 - 250 g pkgs.'      unit_price = '38.00' units_stock = 21  units_order = 10 reorder_level = 30 discontinued = '' )
      ( product_id = 57  product_name = 'Ravioli Angelo'                    supplier_id = 26 category_id = 5 qty_per_unit = '24 - 250 g pkgs.'      unit_price = '19.50' units_stock = 36  units_order = 0  reorder_level = 20 discontinued = '' )
      ( product_id = 58  product_name = 'Escargots de Bourgogne'            supplier_id = 27 category_id = 8 qty_per_unit = '24 pieces'              unit_price = '13.25' units_stock = 62  units_order = 0  reorder_level = 20 discontinued = '' )
      ( product_id = 59  product_name = 'Raclette Courdavault'              supplier_id = 28 category_id = 4 qty_per_unit = '5 kg pkg.'              unit_price = '55.00' units_stock = 79  units_order = 0  reorder_level = 0  discontinued = '' )
      ( product_id = 60  product_name = 'Camembert Pierrot'                 supplier_id = 28 category_id = 4 qty_per_unit = '15 - 300 g rounds'     unit_price = '34.00' units_stock = 19  units_order = 0  reorder_level = 0  discontinued = '' )
      ( product_id = 61  product_name = 'Sirop d''erable'                   supplier_id = 29 category_id = 2 qty_per_unit = '24 - 500 ml bottles'   unit_price = '28.50' units_stock = 113 units_order = 0  reorder_level = 25 discontinued = '' )
      ( product_id = 62  product_name = 'Tarte au sucre'                    supplier_id = 29 category_id = 3 qty_per_unit = '48 pies'               unit_price = '49.30' units_stock = 17  units_order = 0  reorder_level = 0  discontinued = '' )
      ( product_id = 63  product_name = 'Vegie-spread'                      supplier_id = 7  category_id = 2 qty_per_unit = '15 - 625 g jars'       unit_price = '43.90' units_stock = 24  units_order = 0  reorder_level = 5  discontinued = '' )
      ( product_id = 64  product_name = 'Wimmers gute Semmelknodel'         supplier_id = 12 category_id = 5 qty_per_unit = '20 bags x 4 pieces'    unit_price = '33.25' units_stock = 22  units_order = 80 reorder_level = 30 discontinued = '' )
      ( product_id = 65  product_name = 'Louisiana Fiery Hot Pepper Sauce'  supplier_id = 2  category_id = 2 qty_per_unit = '32 - 8 oz bottles'     unit_price = '21.05' units_stock = 76  units_order = 0  reorder_level = 0  discontinued = '' )
      ( product_id = 66  product_name = 'Louisiana Hot Spiced Okra'         supplier_id = 2  category_id = 2 qty_per_unit = '24 - 8 oz jars'        unit_price = '17.00' units_stock = 4   units_order = 100 reorder_level = 20 discontinued = '' )
      ( product_id = 67  product_name = 'Laughing Lumberjack Lager'         supplier_id = 16 category_id = 1 qty_per_unit = '24 - 12 oz bottles'   unit_price = '14.00' units_stock = 52  units_order = 0  reorder_level = 10 discontinued = '' )
      ( product_id = 68  product_name = 'Scottish Longbreads'               supplier_id = 8  category_id = 3 qty_per_unit = '10 boxes x 8 pieces'   unit_price = '12.50' units_stock = 6   units_order = 10 reorder_level = 15 discontinued = '' )
      ( product_id = 69  product_name = 'Gudbrandsdalsost'                  supplier_id = 15 category_id = 4 qty_per_unit = '10 kg pkg.'             unit_price = '36.00' units_stock = 26  units_order = 0  reorder_level = 15 discontinued = '' )
      ( product_id = 70  product_name = 'Outback Lager'                     supplier_id = 7  category_id = 1 qty_per_unit = '24 - 355 ml bottles'   unit_price = '15.00' units_stock = 15  units_order = 10 reorder_level = 30 discontinued = '' )
      ( product_id = 71  product_name = 'Flotemysost'                       supplier_id = 15 category_id = 4 qty_per_unit = '10 - 500 g pkgs.'      unit_price = '21.50' units_stock = 26  units_order = 0  reorder_level = 0  discontinued = '' )
      ( product_id = 72  product_name = 'Mozzarella di Giovanni'            supplier_id = 14 category_id = 4 qty_per_unit = '24 - 200 g pkgs.'      unit_price = '34.80' units_stock = 14  units_order = 0  reorder_level = 0  discontinued = '' )
      ( product_id = 73  product_name = 'Rod Kaviar'                        supplier_id = 17 category_id = 8 qty_per_unit = '24 - 150 g jars'       unit_price = '15.00' units_stock = 101 units_order = 0  reorder_level = 5  discontinued = '' )
      ( product_id = 74  product_name = 'Longlife Tofu'                     supplier_id = 4  category_id = 7 qty_per_unit = '5 kg pkg.'              unit_price = '10.00' units_stock = 4   units_order = 20 reorder_level = 5  discontinued = '' )
      ( product_id = 75  product_name = 'Rhonbrau Klosterbier'              supplier_id = 12 category_id = 1 qty_per_unit = '24 - 0.5 l bottles'    unit_price = '7.75'  units_stock = 125 units_order = 0  reorder_level = 25 discontinued = '' )
      ( product_id = 76  product_name = 'Lakkalikoori'                      supplier_id = 23 category_id = 1 qty_per_unit = '500 ml'                unit_price = '18.00' units_stock = 57  units_order = 0  reorder_level = 20 discontinued = '' )
      ( product_id = 77  product_name = 'Original Frankfurter grune Sosse'  supplier_id = 12 category_id = 2 qty_per_unit = '12 boxes'               unit_price = '13.00' units_stock = 32  units_order = 0  reorder_level = 15 discontinued = '' )
    ).

    " Map to DB table type and insert
    DATA lt_db TYPE TABLE OF znw_products.
    LOOP AT lt_products INTO DATA(ls_p).
      APPEND VALUE #(
        product_id    = ls_p-product_id
        product_name  = ls_p-product_name
        supplier_id   = ls_p-supplier_id
        category_id   = ls_p-category_id
        qty_per_unit  = ls_p-qty_per_unit
        unit_price    = ls_p-unit_price
        units_in_stock = ls_p-units_stock
        units_on_order = ls_p-units_order
        reorder_level = ls_p-reorder_level
        discontinued  = ls_p-discontinued
      ) TO lt_db.
    ENDLOOP.

    DELETE FROM znw_products.
    INSERT znw_products FROM TABLE @lt_db.

    out->write( |Loaded { lines( lt_db ) } Northwind products into ZNW_PRODUCTS| ).

  ENDMETHOD.
ENDCLASS.
