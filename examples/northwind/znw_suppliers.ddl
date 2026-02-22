@EndUserText.label : 'Northwind Suppliers'
@AbapCatalog.enhancement.category : #NOT_EXTENSIBLE
@AbapCatalog.tableCategory : #TRANSPARENT
@AbapCatalog.deliveryClass : #A
@AbapCatalog.dataMaintenance : #RESTRICTED
define table znw_suppliers {

  key client        : abap.clnt not null;
  key supplier_id   : abap.int4 not null;
  company_name      : abap.char(40);
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
