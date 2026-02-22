@EndUserText.label : 'Northwind Customers'
@AbapCatalog.enhancement.category : #NOT_EXTENSIBLE
@AbapCatalog.tableCategory : #TRANSPARENT
@AbapCatalog.deliveryClass : #A
@AbapCatalog.dataMaintenance : #RESTRICTED
define table znw_customers {

  key client        : abap.clnt not null;
  key cust_id       : abap.char(5) not null;
  comp_name         : abap.char(40);
  contact_name      : abap.char(30);
  contact_title     : abap.char(30);
  address           : abap.char(60);
  city              : abap.char(15);
  region            : abap.char(15);
  postal_code       : abap.char(10);
  country           : abap.char(15);
  phone             : abap.char(24);
  fax               : abap.char(24);

}
