@EndUserText.label : 'Northwind Products'
@AbapCatalog.enhancement.category : #NOT_EXTENSIBLE
@AbapCatalog.tableCategory : #TRANSPARENT
@AbapCatalog.deliveryClass : #A
@AbapCatalog.dataMaintenance : #RESTRICTED
define table znw_products {

  key client        : abap.clnt not null;
  key product_id    : abap.int4 not null;
  product_name      : abap.char(40);
  supplier_id       : abap.int4;
  category_id       : abap.int4;
  qty_per_unit      : abap.char(20);
  unit_price        : abap.dec(13,2);
  units_in_stock    : abap.int2;
  units_on_order    : abap.int2;
  reorder_level     : abap.int2;
  discontinued      : abap.char(1);

}
