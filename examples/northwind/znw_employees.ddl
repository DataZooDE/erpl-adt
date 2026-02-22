@EndUserText.label : 'Northwind Employees'
@AbapCatalog.enhancement.category : #NOT_EXTENSIBLE
@AbapCatalog.tableCategory : #TRANSPARENT
@AbapCatalog.deliveryClass : #A
@AbapCatalog.dataMaintenance : #RESTRICTED
define table znw_employees {

  key client        : abap.clnt not null;
  key employee_id   : abap.int4 not null;
  last_name         : abap.char(20);
  first_name        : abap.char(10);
  title             : abap.char(30);
  birth_date        : abap.dats;
  hire_date         : abap.dats;
  address           : abap.char(60);
  city              : abap.char(15);
  region            : abap.char(15);
  postal_code       : abap.char(10);
  country           : abap.char(15);
  home_phone        : abap.char(24);
  reports_to        : abap.int4;

}
