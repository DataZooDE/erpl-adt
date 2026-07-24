@AccessControl.authorizationCheck: #NOT_REQUIRED
@EndUserText.label: 'ABAP Application Component Text'
@VDM.viewType: #BASIC
@ObjectModel.dataCategory:#TEXT
@ObjectModel.representativeKey: 'ABAPApplicationComponent'
define view entity I_ABAPApplCompText
  as select from df14t
  association to parent I_ABAPApplicationComponent as _ABAPApplicationComponent on $projection.ABAPApplicationComponent = _ABAPApplicationComponent.ABAPApplicationComponent
  association [0..1] to I_Language                 as _Language                 on $projection.Language = _Language.Language
{
      @ObjectModel.foreignKey.association: '_Language'
      @Semantics.language: true
  key langu   as Language,
      @ObjectModel.foreignKey.association: '_ABAPApplicationComponent'
  key fctr_id as ABAPApplicationComponent,
      @Semantics.text: true
      name    as ABAPApplicationComponentName,
      // @ObjectModel.association.type: [#TO_COMPOSITION_PARENT, #TO_COMPOSITION_ROOT] -- no more needed
      _ABAPApplicationComponent,
      _Language
}
where
      addon    = ''
  and as4local = 'A'
