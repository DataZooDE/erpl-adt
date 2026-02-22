#!/usr/bin/env bash
# Deploy Northwind Products into SAP ABAP system via erpl-adt
# Usage: SAP_PASSWORD='...' ./deploy.sh [--package ZPKG] [--transport NPLK000001]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ADT="${SCRIPT_DIR}/../build/erpl-adt"

PACKAGE="${PACKAGE:-\$TMP}"
TRANSPORT_ARG=""

while [[ $# -gt 0 ]]; do
  case $1 in
    --package)  PACKAGE="$2";  shift 2 ;;
    --transport) TRANSPORT_ARG="--transport $2"; shift 2 ;;
    *) echo "Unknown arg: $1"; exit 1 ;;
  esac
done

[[ -n "$TRANSPORT_ARG" ]] || true  # optional

echo "==> Creating transparent table ZNW_PRODUCTS (TABL/DT) in $PACKAGE"
$ADT object create \
  --type TABL/DT \
  --name ZNW_PRODUCTS \
  --package "$PACKAGE" \
  --description "Northwind Products" \
  $TRANSPORT_ARG

echo "==> Writing table DDL source"
$ADT source write /sap/bc/adt/ddic/tables/znw_products/source/main \
  --file "$SCRIPT_DIR/znw_products.ddl" \
  --activate \
  $TRANSPORT_ARG

echo "==> Creating CDS view ZNW_PRODUCTS_V (DDLS/DF) in $PACKAGE"
$ADT object create \
  --type DDLS/DF \
  --name ZNW_PRODUCTS_V \
  --package "$PACKAGE" \
  --description "Northwind Products View" \
  $TRANSPORT_ARG

echo "==> Writing CDS view source"
$ADT source write /sap/bc/adt/ddic/ddl/sources/znw_products_v/source/main \
  --file "$SCRIPT_DIR/znw_products_v.ddls" \
  --activate \
  $TRANSPORT_ARG

echo "==> Creating ABAP class ZCL_NORTHWIND_GEN (CLAS/OC) in $PACKAGE"
$ADT object create \
  --type CLAS/OC \
  --name ZCL_NORTHWIND_GEN \
  --package "$PACKAGE" \
  --description "Load Northwind Products data" \
  $TRANSPORT_ARG

echo "==> Writing class source"
$ADT source write /sap/bc/adt/oo/classes/zcl_northwind_gen/source/main \
  --file "$SCRIPT_DIR/zcl_northwind_gen.abap" \
  --activate \
  $TRANSPORT_ARG

echo "==> Running class to populate ZNW_PRODUCTS"
$ADT object run ZCL_NORTHWIND_GEN

echo ""
echo "Done. Read the data with:"
echo "  rfc_read_table on ZNW_PRODUCTS or ZNW_PRODUCTS_V"
