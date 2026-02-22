# Northwind Example

Deploys the classic Northwind Traders sample database into an ABAP Cloud system
using erpl-adt.

## Tables deployed

| Table               | Rows | Description             |
|---------------------|-----:|-------------------------|
| znw_categories      |    8 | Product categories       |
| znw_suppliers       |   29 | Product suppliers        |
| znw_customers       |   91 | Customer accounts        |
| znw_employees       |    9 | Sales staff              |
| znw_shippers        |    3 | Freight companies        |
| znw_products        |   77 | Product catalog          |
| znw_orders          |  830 | Customer orders          |
| znw_order_details   | 2155 | Order line items         |

Each table has a corresponding CDS view entity (`znw_*_v`) for consumption.

## Usage

```bash
SAP_PASSWORD='...' ./deploy.sh [--package ZNORTHWIND] [--parent ZLOCAL] [--transport NPLK...]
```

The script creates the package, deploys all tables and views, then runs each
data loader class to populate the tables.

## Prerequisites

- `erpl-adt` binary built at `../../build/erpl-adt`
- Connection credentials: set `SAP_PASSWORD` plus optional `SAP_HOST`, `SAP_PORT`,
  `SAP_USER`, `SAP_CLIENT` environment variables, or pass them as CLI flags
- Target package parent (default `ZLOCAL`) must already exist
- For transport-tracked deployments, provide an open transport request via `--transport`

## File structure

```
znw_*.ddl          Table DDL (CDS TABL/DT syntax)
znw_*_v.ddls       CDS view entities
zcl_nw_*.abap      Data loader classes (if_oo_adt_classrun)
deploy.sh          Orchestration script
```
