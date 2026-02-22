@EndUserText.label : 'Northwind Categories'
@AbapCatalog.enhancement.category : #NOT_EXTENSIBLE
@AbapCatalog.tableCategory : #TRANSPARENT
@AbapCatalog.deliveryClass : #A
@AbapCatalog.dataMaintenance : #RESTRICTED
define table znw_categories {

  key client        : abap.clnt not null;
  key category_id   : abap.int4 not null;
  category_name     : abap.char(15);
  description       : abap.char(100);

}
