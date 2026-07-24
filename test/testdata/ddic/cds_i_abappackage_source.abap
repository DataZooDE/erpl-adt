@AccessControl.authorizationCheck: #CHECK
@EndUserText.label: 'ABAP Package'
@VDM.viewType: #BASIC
@ObjectModel.compositionRoot: true
@ObjectModel.representativeKey: 'ABAPPackage'
define root view entity I_ABAPPackage
  as select from tdevc
  association [1..1] to I_ABAPSoftwareComponent    as _ABAPSoftwareComponent    on $projection.ABAPSoftwareComponent = _ABAPSoftwareComponent.ABAPSoftwareComponent
  association [1..1] to I_ABAPApplicationComponent as _ABAPApplicationComponent on $projection.ABAPApplicationComponent = _ABAPApplicationComponent.ABAPApplicationComponent
  composition [0..*] of I_ABAPPackageText          as _Text                     
{
      @ObjectModel.text.association: '_Text'
  key devclass     as ABAPPackage,
      as4user      as ABAPPackageResponsibleUser,
      dlvunit      as ABAPSoftwareComponent,
      component    as ABAPApplicationComponent,
      namespace    as ABAPNamespace,
      packtype     as ABAPPackageTargetEnvironment,
      created_by   as CreatedByUser,
      created_on   as CreationDate,
      changed_by   as LastChangedByUser,
      changed_on   as LastChangeDate,
      package_kind as ABAPLanguageVersion,
      _ABAPSoftwareComponent,
      _ABAPApplicationComponent,
      // @ObjectModel.association.type: [#TO_COMPOSITION_CHILD] -- should no more necessary
      _Text
}
