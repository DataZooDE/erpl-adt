@EndUserText.label : 'Northwind Orders'
@AbapCatalog.enhancement.category : #NOT_EXTENSIBLE
@AbapCatalog.tableCategory : #TRANSPARENT
@AbapCatalog.deliveryClass : #A
@AbapCatalog.dataMaintenance : #RESTRICTED
define table znw_orders {

  key client           : abap.clnt not null;
  key order_id         : abap.int4 not null;
  cust_id              : abap.char(5);
  employee_id          : abap.int4;
  order_date           : abap.dats;
  required_date        : abap.dats;
  shipped_date         : abap.dats;
  ship_via             : abap.int4;
  freight              : abap.dec(13,4);
  ship_name            : abap.char(40);
  ship_address         : abap.char(60);
  ship_city            : abap.char(15);
  ship_region          : abap.char(15);
  ship_postal_code     : abap.char(10);
  ship_country         : abap.char(15);

}
