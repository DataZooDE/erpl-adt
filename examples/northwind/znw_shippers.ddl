@EndUserText.label : 'Northwind Shippers'
@AbapCatalog.enhancement.category : #NOT_EXTENSIBLE
@AbapCatalog.tableCategory : #TRANSPARENT
@AbapCatalog.deliveryClass : #A
@AbapCatalog.dataMaintenance : #RESTRICTED
define table znw_shippers {

  key client        : abap.clnt not null;
  key shipper_id    : abap.int4 not null;
  company_name      : abap.char(40);
  phone             : abap.char(24);

}
