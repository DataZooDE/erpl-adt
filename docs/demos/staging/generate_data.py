#!/usr/bin/env python3
"""Regenerate canned demo output files with real ANSI escape sequences.

Run from this directory:  python3 generate_data.py

The files this script writes mirror the output of the real erpl-adt binary
against a live SAP ABAP Cloud system, but are reproducible offline.
"""
from pathlib import Path

ESC = "\x1b"
B = f"{ESC}[1m"     # bold on
NB = f"{ESC}[22m"   # bold off
CYAN = f"{ESC}[36m"
GREEN = f"{ESC}[1;32m"
DIM = f"{ESC}[90m"
RESET = f"{ESC}[0m"

DATA = Path(__file__).parent / "data"
DATA.mkdir(exist_ok=True)


def bh(text: str, width: int) -> str:
    return f"{B}{text.ljust(width)}{NB}"


def write_table(name: str, headers, widths, rows):
    """Write an FTXUI-style table with bold headers and light bottom border."""
    parts = [f"{bh(h, w)}" for h, w in zip(headers, widths)]
    head = "│".join(parts)
    border = "┴".join("─" * w for w in widths)
    out = [head, border]
    for row in rows:
        out.append(" ".join(cell.ljust(w) for cell, w in zip(row, widths)).rstrip())
    (DATA / name).write_text("\n".join(out) + "\n")


# === search ===
write_table(
    "search.txt",
    ["Name", "Type", "Package", "Description"],
    [30, 8, 27, 40],
    [
        ("CL_DEMO_ABAP_DAEMON",         "CLAS/OC", "SABAPDEMOS",                "ABAP Daemon Demo"),
        ("CL_DEMO_ABAP_DAEMON_BROKER",  "CLAS/OC", "SABAPDEMOS",                "ABAP Daemon Demo broker"),
        ("CL_DEMO_ABAP_OBJECTS",        "CLAS/OC", "SABAP_DEMOS_NOT_FOR_CLOUD", "Demo for ABAP Keyword Documentation"),
        ("CL_DEMO_AMDP_METHODS",        "CLAS/OC", "SABAPDEMOS",                "AMDP demo"),
        ("CL_DEMO_OUTPUT",              "CLAS/OC", "SABAP_DEMOS_OUTPUT_STREAM", "Formatted Output of Example Data"),
        ("CL_DEMO_OUTPUT_HTML",         "CLAS/OC", "SABAP_DEMOS_OUTPUT_STREAM", "Converts demo output stream to HTML"),
        ("CL_DEMO_RAP_FLIGHT_M_TRAVEL", "CLAS/OC", "SABAP_DEMOS_RAP_FLIGHT",    "Behavior for Managed Travel"),
        ("CL_DEMO_RAP_LOGGING",         "CLAS/OC", "SABAP_DEMOS_RAP_GENERIC",   "RAP logging demo helper"),
    ],
)


# === ddic table SFLIGHT ===
def write_ddic():
    headers = ["Field", "DataElem", "AbapType", "Key", "Length", "Description", "CheckTable"]
    widths  = [13, 12, 8, 3, 6, 35, 11]
    rows = [
        ("mandt",      "s_mandt",     "CLNT", "Y", "3",    "Client",                              "T000"),
        ("carrid",     "s_carr_id",   "CHAR", "Y", "3",    "Airline Code",                        "SCARR"),
        ("connid",     "s_conn_id",   "NUMC", "Y", "4",    "Flight Connection Number",            "SPFLI"),
        ("fldate",     "s_date",      "DATS", "Y", "8",    "Flight date",                         ""),
        ("price",      "s_price",     "CURR", "",  "15,2", "Airfare",                             ""),
        ("currency",   "s_currcod",   "CUKY", "",  "5",    "Local currency of airline",           "SCURX"),
        ("planetype",  "s_planety",   "CHAR", "",  "10",   "Aircraft Type",                       "SAPLANE"),
        ("seatsmax",   "s_seatsmax",  "INT4", "",  "10",   "Maximum capacity in economy class",   ""),
        ("seatsocc",   "s_seatsocc",  "INT4", "",  "10",   "Occupied seats in economy class",     ""),
        ("paymentsum", "s_sum",       "CURR", "",  "17,2", "Total of current bookings",           ""),
        ("seatsmax_b", "s_smax_b",    "INT4", "",  "10",   "Maximum capacity in business class",  ""),
        ("seatsocc_b", "s_socc_b",    "INT4", "",  "10",   "Occupied seats in business class",    ""),
        ("seatsmax_f", "s_smax_f",    "INT4", "",  "10",   "Maximum capacity in first class",     ""),
        ("seatsocc_f", "s_socc_f",    "INT4", "",  "10",   "Occupied seats in first class",       ""),
    ]
    head = "│".join(bh(h, w) for h, w in zip(headers, widths))
    border = "┴".join("─" * w for w in widths)
    body = "\n".join(" ".join(c.ljust(w) for c, w in zip(row, widths)).rstrip()
                     for row in rows)
    (DATA / "ddic-table.txt").write_text(
        "SFLIGHT [A] - Flight\n" + head + "\n" + border + "\n" + body + "\n")


write_ddic()


# === package tree ===
write_table(
    "package-tree.txt",
    ["Type", "Name", "Package", "Description"],
    [8, 27, 25, 45],
    [
        ("CLAS/OC", "CL_DEMO_OUTPUT",           "SABAP_DEMOS_OUTPUT_STREAM", "Formatted Output of Example Data"),
        ("CLAS/OC", "CL_DEMO_OUTPUT_HELPER",    "SABAP_DEMOS_OUTPUT_STREAM", "Helper class for CL_DEMO_OUTPUT"),
        ("CLAS/OC", "CL_DEMO_OUTPUT_HTML",      "SABAP_DEMOS_OUTPUT_STREAM", "Converts demo output stream to HTML"),
        ("CLAS/OC", "CL_DEMO_OUTPUT_JSON",      "SABAP_DEMOS_OUTPUT_STREAM", "Converts demo output stream to JSON"),
        ("CLAS/OC", "CL_DEMO_OUTPUT_STREAM",    "SABAP_DEMOS_OUTPUT_STREAM", "Demo for XML Output Stream for ABAP"),
        ("CLAS/OC", "CL_DEMO_OUTPUT_TEXT",      "SABAP_DEMOS_OUTPUT_STREAM", "Converts demo output stream to text"),
        ("CLAS/OC", "CL_DEMO_OUTPUT_XML",       "SABAP_DEMOS_OUTPUT_STREAM", "Converts demo output stream to XML"),
        ("INTF/OI", "IF_DEMO_OUTPUT",           "SABAP_DEMOS_OUTPUT_STREAM", "Methods for Formatted Output"),
        ("INTF/OI", "IF_DEMO_OUTPUT_FORMATS",   "SABAP_DEMOS_OUTPUT_STREAM", "Constants for Demo Output Stream Format"),
        ("PROG/P",  "DEMO_OUTPUT_STREAM",       "SABAP_DEMOS_OUTPUT_STREAM", "Accessing CL_DEMO_OUTPUT_STREAM"),
        ("PROG/P",  "DEMO_USAGE_OUTPUT_GET",    "SABAP_DEMOS_OUTPUT_STREAM", "Use of Method GET of CL_DEMO_OUTPUT"),
        ("PROG/P",  "DEMO_USAGE_OUTPUT_STATIC", "SABAP_DEMOS_OUTPUT_STREAM", "Use Static Methods from CL_DEMO_OUTPUT"),
        ("TABL/DT", "DEMO_INFO_HTML",           "SABAP_DEMOS_OUTPUT_STREAM", "HTML Snippets for Info Texts"),
    ],
)


# === source read CL_DEMO_OUTPUT (head of the file — fits demo terminal) ===
SRC = f"""\
{CYAN}class{RESET} CL_DEMO_OUTPUT definition {CYAN}public{RESET} {CYAN}final{RESET} {CYAN}create{RESET} {CYAN}private{RESET} .

{CYAN}public{RESET} {CYAN}section{RESET}.

  {CYAN}interfaces{RESET} IF_DEMO_OUTPUT_FORMATS .
  {CYAN}interfaces{RESET} IF_DEMO_OUTPUT .

  {CYAN}constants{RESET} HTML_MODE {CYAN}type{RESET} STRING {CYAN}value{RESET} {GREEN}'HTML'{RESET} ##NO_TEXT.
  {CYAN}constants{RESET} TEXT_MODE {CYAN}type{RESET} STRING {CYAN}value{RESET} {GREEN}'TEXT'{RESET} ##NO_TEXT.
  {CYAN}constants{RESET} JSON_MODE {CYAN}type{RESET} STRING {CYAN}value{RESET} {GREEN}'JSON'{RESET} ##NO_TEXT.

  {CYAN}class-methods{RESET} DISPLAY
    {CYAN}importing{RESET}
      !{CYAN}DATA{RESET}    {CYAN}type{RESET} ANY    {CYAN}optional{RESET}
      !NAME    {CYAN}type{RESET} STRING {CYAN}optional{RESET}
    preferred parameter {CYAN}DATA{RESET} .

  {CYAN}class-methods{RESET} WRITE
    {CYAN}importing{RESET} !{CYAN}DATA{RESET} {CYAN}type{RESET} ANY {CYAN}optional{RESET}
    preferred parameter {CYAN}DATA{RESET} .

{DIM}* ... (35 more methods){RESET}
"""
(DATA / "source-read.txt").write_text(SRC)

# === bw search ===
# BW search returns: Name, Type, Status, Description, Changed, URI
write_table(
    "bw-search.txt",
    ["Name", "Type", "Status", "Description", "Changed"],
    [22, 6, 7, 38, 12],
    [
        ("ZSD_SALES_ORDER",    "ADSO", "ACT",    "Sales Order DSO",                       "2026-04-12"),
        ("ZSD_SALES_ITEM",     "ADSO", "ACT",    "Sales Order Items",                     "2026-04-12"),
        ("ZSD_SALES_HIST",     "ADSO", "ACT",    "Sales History (Snapshot)",              "2026-03-28"),
        ("ZC_SD_SALES_CUBE",   "HCPR", "ACT",    "Sales — Composite Provider",            "2026-04-12"),
        ("DTP_SD_O_TO_C",      "DTPA", "ACT",    "DTP: O_SALES_ORDER → C_SD_SALES_CUBE",  "2026-04-12"),
        ("TRFN_SD_O_TO_C",     "TRFN", "ACT",    "Transformation O_SALES → C_SALES",      "2026-04-12"),
        ("RSDS_SD_S4HANA",     "RSDS", "ACT",    "DataSource: SD orders from S/4HANA",    "2026-02-10"),
        ("ZQ_SD_TOP_PRODUCTS", "QUERY","INA",    "Top products by revenue (draft)",       "2026-05-10"),
    ],
)


# === bw read-adso ===
def write_bw_read_adso():
    headers = ["Field", "Type", "Length", "Key", "InfoObject"]
    widths  = [22, 8, 8, 5, 24]
    rows = [
        ("CALMONTH",      "NUMC",  "6",   "X", "0CALMONTH"),
        ("SOLD_TO_PARTY", "CHAR",  "10",  "X", "0SOLD_TO"),
        ("MATERIAL",      "CHAR",  "40",  "X", "0MATERIAL"),
        ("SALES_ORG",     "CHAR",  "4",   "X", "0SALESORG"),
        ("DISTR_CHAN",    "CHAR",  "2",   "X", "0DISTR_CHAN"),
        ("DIVISION",      "CHAR",  "2",   "X", "0DIVISION"),
        ("CURRENCY",      "CUKY",  "5",   "",  "0CURRENCY"),
        ("NETVAL_INV",    "CURR",  "17,2", "", "0NETVAL_INV"),
        ("QUANTITY_B",    "QUAN",  "17,3", "", "0QUANT_B"),
        ("BASE_UNIT",     "UNIT",  "3",   "",  "0BASE_UOM"),
        ("REC_TYPE",      "CHAR",  "1",   "",  "0RECORDTYPE"),
    ]
    head = "│".join(bh(h, w) for h, w in zip(headers, widths))
    border = "┴".join("─" * w for w in widths)
    body = "\n".join(" ".join(c.ljust(w) for c, w in zip(row, widths)).rstrip()
                     for row in rows)
    out = (
        "ADSO: ZSD_SALES_ORDER\n"
        "  Description: Sales Order DSO\n"
        "  Package:     ZBW_SD_DEMO\n\n"
        + head + "\n" + border + "\n" + body + "\n"
    )
    (DATA / "bw-read-adso.txt").write_text(out)


write_bw_read_adso()


# === bw read-dtp ===
DTP_OUT = f"""\
DTP: DTP_SD_O_TO_C
  Description: DTP: ZSD_SALES_ORDER → ZC_SD_SALES_CUBE
  Type:        Standard
  Source:      ZSD_SALES_ORDER (ADSO)
  Target:      ZC_SD_SALES_CUBE (HCPR)
  Mode:        {CYAN}delta{RESET} (request-based)
  Source System: BWQ100

  {B}Extraction{NB}
    Package size: 50,000
    Parallel processes: 3
    Filter: CALMONTH ≥ 202501

  {B}Execution{NB}
    Selection mode: byRequest
    Get data by request: yes
    Error handling: stopOnError

  {B}Runtime{NB}
    Last successful run: 2026-05-15 02:00
    Avg duration:        00:04:32
    Avg records:         842,108
"""
(DATA / "bw-read-dtp.txt").write_text(DTP_OUT)


# === bw export-cube --mermaid ===
MMD_OUT = """\
%%{init: {'flowchart': {'curve': 'basis'}}}%%
graph LR

  subgraph Sources
    RSDS_SD_S4HANA["RSDS_SD_S4HANA<br/>DataSource: SD orders from S/4HANA"]
  end

  subgraph Staging[ZBW_SD_DEMO]
    ZSD_SALES_ORDER["ZSD_SALES_ORDER<br/>Sales Order DSO"]
    ZSD_SALES_ITEM["ZSD_SALES_ITEM<br/>Sales Order Items"]
    ZSD_SALES_HIST["ZSD_SALES_HIST<br/>Sales History (Snapshot)"]
  end

  subgraph InfoCubes
    ZC_SD_SALES_CUBE["ZC_SD_SALES_CUBE<br/>Sales — Composite Provider"]
  end

  subgraph Queries
    ZQ_SD_TOP_PRODUCTS["ZQ_SD_TOP_PRODUCTS<br/>Top products by revenue"]
    ZQ_SD_REVENUE_TR["ZQ_SD_REVENUE_TR<br/>Revenue Trend (12m)"]
  end

  RSDS_SD_S4HANA --> ZSD_SALES_ORDER
  RSDS_SD_S4HANA --> ZSD_SALES_ITEM
  ZSD_SALES_ORDER --> ZSD_SALES_HIST
  ZSD_SALES_ORDER --> ZC_SD_SALES_CUBE
  ZSD_SALES_ITEM --> ZC_SD_SALES_CUBE
  ZC_SD_SALES_CUBE --> ZQ_SD_TOP_PRODUCTS
  ZC_SD_SALES_CUBE --> ZQ_SD_REVENUE_TR
"""
(DATA / "bw-export-cube.txt").write_text(MMD_OUT)


print(f"Wrote {len(list(DATA.glob('*.txt')))} files into {DATA}")
