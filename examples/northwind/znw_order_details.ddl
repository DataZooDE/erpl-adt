@EndUserText.label : 'Northwind Order Details'
@AbapCatalog.enhancement.category : #NOT_EXTENSIBLE
@AbapCatalog.tableCategory : #TRANSPARENT
@AbapCatalog.deliveryClass : #A
@AbapCatalog.dataMaintenance : #RESTRICTED
define table znw_order_details {

  key client        : abap.clnt not null;
  key order_id      : abap.int4 not null;
  key product_id    : abap.int4 not null;
  unit_price        : abap.dec(13,4);
  quantity          : abap.int2;
  discount          : abap.dec(8,4);

}
