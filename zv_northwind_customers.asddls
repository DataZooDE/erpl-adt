@AbapCatalog.sqlViewName: 'ZNWCUSTV'
@AbapCatalog.compiler.compareFilter: true
@AbapCatalog.preserveKey: true
@AccessControl.authorizationCheck: #NOT_REQUIRED
@EndUserText.label: 'Northwind Customers'
define view ZV_NORTHWIND_CUSTOMERS
  as select from znorthwind_cust
{
  key cust_id       as CustomerID,
      comp_name     as CompanyName,
      contact_name  as ContactName,
      contact_title as ContactTitle,
      address       as Address,
      city          as City,
      region        as Region,
      postal_code   as PostalCode,
      country       as Country,
      phone         as Phone,
      fax           as Fax
}
