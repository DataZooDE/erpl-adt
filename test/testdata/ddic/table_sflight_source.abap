@EndUserText.label : 'Flug'
@AbapCatalog.enhancement.category : #EXTENSIBLE_CHARACTER_NUMERIC
@AbapCatalog.tableCategory : #TRANSPARENT
@AbapCatalog.deliveryClass : #A
@AbapCatalog.dataMaintenance : #ALLOWED
define table sflight {

  @AbapCatalog.foreignKey.keyType : #KEY
  @AbapCatalog.foreignKey.screenCheck : true
  key mandt  : s_mandt not null
    with foreign key [0..*,1] t000
      where mandt = sflight.mandt;
  @AbapCatalog.foreignKey.screenCheck : true
  key carrid : s_carr_id not null
    with foreign key scarr
      where mandt = sflight.mandt
        and carrid = sflight.carrid;
  @AbapCatalog.foreignKey.label : 'Prüfung gegen Flugplan'
  @AbapCatalog.foreignKey.keyType : #KEY
  @AbapCatalog.foreignKey.screenCheck : true
  key connid : s_conn_id not null
    with foreign key [0..*,1] spfli
      where mandt = sflight.mandt
        and carrid = sflight.carrid
        and connid = sflight.connid;
  key fldate : s_date not null;
  @Semantics.amount.currencyCode : 'sflight.currency'
  price      : s_price not null;
  @AbapCatalog.foreignKey.keyType : #NON_KEY
  @AbapCatalog.foreignKey.screenCheck : true
  currency   : s_currcode not null
    with foreign key [1..*,1] scurx
      where currkey = sflight.currency;
  @AbapCatalog.foreignKey.label : 'Prüfung gegen Flugzeug'
  @AbapCatalog.foreignKey.keyType : #NON_KEY
  @AbapCatalog.foreignKey.screenCheck : true
  planetype  : s_planetye not null
    with foreign key [0..*,1] saplane
      where mandt = sflight.mandt
        and planetype = sflight.planetype;
  seatsmax   : s_seatsmax not null;
  seatsocc   : s_seatsocc not null;
  @Semantics.amount.currencyCode : 'sflight.currency'
  paymentsum : s_sum not null;
  seatsmax_b : s_smax_b not null;
  seatsocc_b : s_socc_b not null;
  seatsmax_f : s_smax_f not null;
  seatsocc_f : s_socc_f not null;

}