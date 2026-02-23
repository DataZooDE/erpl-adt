#!/usr/bin/env bash
# Deploy full Northwind database into SAP ABAP system via erpl-adt
#
# Usage:
#   SAP_PASSWORD='...' ./deploy.sh [--package ZNORTHWIND] [--parent ZLOCAL] \
#       [--transport NPLK...] [-- <erpl-adt global flags>]
#
# Connection credentials:
#   erpl-adt reads connection details from (in priority order):
#     1. Flags after --  e.g. -- --host myhost --port 50000 --user DEV --client 001
#     2. ~/.adt.creds    run: erpl-adt login  (saves credentials to .adt.creds)
#     3. Defaults        localhost:50000, DEVELOPER, client 001
#   SAP_PASSWORD env var is read by erpl-adt when --password is not given.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ADT="${SCRIPT_DIR}/../../build/erpl-adt"

PACKAGE="${PACKAGE:-ZNORTHWIND}"
PARENT="${PARENT:-ZLOCAL}"
TRANSPORT=()

# Collect erpl-adt global flags that follow --
ADT_FLAGS=()

while [[ $# -gt 0 ]]; do
  case $1 in
    --package)    PACKAGE="$2";              shift 2 ;;
    --parent)     PARENT="$2";               shift 2 ;;
    --transport)  TRANSPORT=(--transport "$2"); shift 2 ;;
    --)           shift; ADT_FLAGS=("$@");   break ;;
    *) echo "Unknown arg: $1  (pass erpl-adt global flags after --)"; exit 1 ;;
  esac
done

# ─── helpers ─────────────────────────────────────────────────────────────────

adt() { "$ADT" "${ADT_FLAGS[@]}" "$@"; }

create_table() {
  local name="$1" desc="$2" file="$3"
  echo "==> Creating table $name"
  adt object create --type TABL/DT --name "$name" --package "$PACKAGE" \
    --description "$desc" "${TRANSPORT[@]}" \
    || echo "    (already exists — updating source)"
  adt source write /sap/bc/adt/ddic/tables/${name,,}/source/main \
    --file "$SCRIPT_DIR/$file" --activate "${TRANSPORT[@]}"
}

create_view() {
  local name="$1" desc="$2" file="$3"
  echo "==> Creating CDS view $name"
  adt object create --type DDLS/DF --name "$name" --package "$PACKAGE" \
    --description "$desc" "${TRANSPORT[@]}" \
    || echo "    (already exists — updating source)"
  adt source write /sap/bc/adt/ddic/ddl/sources/${name,,}/source/main \
    --file "$SCRIPT_DIR/$file" --activate "${TRANSPORT[@]}"
}

create_loader() {
  local name="$1" desc="$2" file="$3"
  echo "==> Creating data loader class $name"
  adt object create --type CLAS/OC --name "$name" --package "$PACKAGE" \
    --description "$desc" "${TRANSPORT[@]}" \
    || echo "    (already exists — updating source)"
  adt source write /sap/bc/adt/oo/classes/${name,,}/source/main \
    --file "$SCRIPT_DIR/$file" --activate "${TRANSPORT[@]}"
  echo "==> Running $name"
  adt object run "$name"
}

# ─── 1. Create package ────────────────────────────────────────────────────────

if [[ "$PACKAGE" == '$TMP' || "$PACKAGE" == "\$TMP" ]]; then
  echo "==> Using local package \$TMP (skipping package creation)"
else
  echo "==> Creating package $PACKAGE under $PARENT"
  adt object create --type DEVC/K --name "$PACKAGE" --package "$PARENT" \
    --description "Northwind Sample Database" "${TRANSPORT[@]}" \
    || echo "    Warning: package creation failed (may already exist — continuing)"
fi

# ─── 2. Tables (dependency order) ────────────────────────────────────────────

create_table ZNW_CATEGORIES   "Northwind Categories"    znw_categories.ddl
create_table ZNW_SUPPLIERS     "Northwind Suppliers"    znw_suppliers.ddl
create_table ZNW_CUSTOMERS     "Northwind Customers"    znw_customers.ddl
create_table ZNW_EMPLOYEES     "Northwind Employees"    znw_employees.ddl
create_table ZNW_SHIPPERS      "Northwind Shippers"     znw_shippers.ddl
create_table ZNW_PRODUCTS      "Northwind Products"     znw_products.ddl
create_table ZNW_ORDERS        "Northwind Orders"       znw_orders.ddl
create_table ZNW_ORD_DETAILS   "Northwind Order Details" znw_ord_details.ddl

# ─── 3. CDS views ────────────────────────────────────────────────────────────

create_view ZNW_CATEGORIES_V   "Northwind Categories"    znw_categories_v.ddls
create_view ZNW_SUPPLIERS_V    "Northwind Suppliers"     znw_suppliers_v.ddls
create_view ZNW_CUSTOMERS_V    "Northwind Customers"     znw_customers_v.ddls
create_view ZNW_EMPLOYEES_V    "Northwind Employees"     znw_employees_v.ddls
create_view ZNW_SHIPPERS_V     "Northwind Shippers"      znw_shippers_v.ddls
create_view ZNW_PRODUCTS_V     "Northwind Products"      znw_products_v.ddls
create_view ZNW_ORDERS_V       "Northwind Orders"        znw_orders_v.ddls
create_view ZNW_ORDER_DETAILS_V "Northwind Order Details" znw_order_details_v.ddls
create_view ZNW_ORDER_ITEMS_V  "Northwind Order Items: orders, details, customers, products" znw_order_items_v.ddls

# ─── 4. Data loader classes ───────────────────────────────────────────────────

create_loader ZCL_NW_CATEGORIES   "Load Northwind Categories"    zcl_nw_categories.abap
create_loader ZCL_NW_SUPPLIERS    "Load Northwind Suppliers"     zcl_nw_suppliers.abap
create_loader ZCL_NW_CUSTOMERS    "Load Northwind Customers"     zcl_nw_customers.abap
create_loader ZCL_NW_EMPLOYEES    "Load Northwind Employees"     zcl_nw_employees.abap
create_loader ZCL_NW_SHIPPERS     "Load Northwind Shippers"      zcl_nw_shippers.abap
create_loader ZCL_NW_PRODUCTS     "Load Northwind Products"      zcl_nw_products.abap
create_loader ZCL_NW_ORDERS       "Load Northwind Orders"        zcl_nw_orders.abap
create_loader ZCL_NW_ORDER_DETAILS "Load Northwind Order Details" zcl_nw_order_details.abap

# ─── 5. Summary ──────────────────────────────────────────────────────────────

echo ""
echo "Done. Northwind database deployed to package $PACKAGE."
echo ""
echo "Tables and row counts:"
echo "  ZNW_CATEGORIES     8 rows"
echo "  ZNW_SUPPLIERS     29 rows"
echo "  ZNW_CUSTOMERS     91 rows"
echo "  ZNW_EMPLOYEES      9 rows"
echo "  ZNW_SHIPPERS       3 rows"
echo "  ZNW_PRODUCTS      77 rows"
echo "  ZNW_ORDERS       830 rows"
echo "  ZNW_ORDER_DETAILS 2155 rows"
echo ""
echo "Query via CDS views: ZNW_CATEGORIES_V, ZNW_CUSTOMERS_V, ZNW_PRODUCTS_V, ..."
echo "  Joined view:        ZNW_ORDER_ITEMS_V (orders + details + customers + products)"
