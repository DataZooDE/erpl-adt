CLASS zcl_nw_employees DEFINITION
  PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    INTERFACES if_oo_adt_classrun.
ENDCLASS.

CLASS zcl_nw_employees IMPLEMENTATION.
  METHOD if_oo_adt_classrun~main.

    DATA lt_data TYPE TABLE OF znw_employees WITH EMPTY KEY.

    lt_data = VALUE #(
      ( employee_id = 1 last_name = 'Davolio'   first_name = 'Nancy'    title = 'Sales Representative'        birth_date = '19481208' hire_date = '19920501' address = '507 - 20th Ave. E. Apt. 2A' city = 'Seattle'  region = 'WA' postal_code = '98122' country = 'USA' home_phone = '(206) 555-9857' reports_to = 2 )
      ( employee_id = 2 last_name = 'Fuller'    first_name = 'Andrew'   title = 'Vice President, Sales'       birth_date = '19520219' hire_date = '19920814' address = '908 W. Capital Way'          city = 'Tacoma'  region = 'WA' postal_code = '98401' country = 'USA' home_phone = '(206) 555-9482' reports_to = 0 )
      ( employee_id = 3 last_name = 'Leverling' first_name = 'Janet'    title = 'Sales Representative'        birth_date = '19630830' hire_date = '19920401' address = '722 Moss Bay Blvd.'          city = 'Kirkland' region = 'WA' postal_code = '98033' country = 'USA' home_phone = '(206) 555-3412' reports_to = 2 )
      ( employee_id = 4 last_name = 'Peacock'   first_name = 'Margaret' title = 'Sales Representative'        birth_date = '19370919' hire_date = '19930503' address = '4110 Old Redmond Rd.'        city = 'Redmond' region = 'WA' postal_code = '98052' country = 'USA' home_phone = '(206) 555-8122' reports_to = 2 )
      ( employee_id = 5 last_name = 'Buchanan'  first_name = 'Steven'   title = 'Sales Manager'               birth_date = '19550304' hire_date = '19931017' address = '14 Garrett Hill'             city = 'London'   region = '' postal_code = 'SW1 8JR' country = 'UK' home_phone = '(71) 555-4848' reports_to = 2 )
      ( employee_id = 6 last_name = 'Suyama'    first_name = 'Michael'  title = 'Sales Representative'        birth_date = '19630702' hire_date = '19931017' address = 'Coventry House Miner Rd.'    city = 'London'   region = '' postal_code = 'EC2 7JR' country = 'UK' home_phone = '(71) 555-7773' reports_to = 5 )
      ( employee_id = 7 last_name = 'King'      first_name = 'Robert'   title = 'Sales Representative'        birth_date = '19600529' hire_date = '19940102' address = 'Edgeham Hollow Winchester Way' city = 'London'  region = '' postal_code = 'RG1 9SP' country = 'UK' home_phone = '(71) 555-5598' reports_to = 5 )
      ( employee_id = 8 last_name = 'Callahan'  first_name = 'Laura'    title = 'Inside Sales Coordinator'    birth_date = '19580109' hire_date = '19940305' address = '4726 - 11th Ave. N.E.'       city = 'Seattle' region = 'WA' postal_code = '98105' country = 'USA' home_phone = '(206) 555-1189' reports_to = 2 )
      ( employee_id = 9 last_name = 'Dodsworth' first_name = 'Anne'     title = 'Sales Representative'        birth_date = '19660127' hire_date = '19941115' address = '7 Houndstooth Rd.'           city = 'London'   region = '' postal_code = 'WG2 7LT' country = 'UK' home_phone = '(71) 555-4444' reports_to = 5 )
    ).

    DELETE FROM znw_employees.
    INSERT znw_employees FROM TABLE @lt_data.

    out->write( |Loaded { lines( lt_data ) } Northwind employees into ZNW_EMPLOYEES| ).

  ENDMETHOD.
ENDCLASS.
