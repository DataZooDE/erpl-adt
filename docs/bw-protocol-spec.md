# SAP BW/4HANA Modeling REST Protocol Specification

Version 1.0 -- February 2026

---

## 1. Introduction

### 1.1 Purpose

This document is the protocol reference for building REST clients that interact with SAP BW/4HANA Modeling endpoints. It documents the HTTP methods, URL paths, query parameters, request/response bodies, XML namespaces, media types, and behavioral patterns required to programmatically model BW objects (DataStore Objects, CompositeProviders, InfoObjects, Transformations, DTPs, Queries, etc.) against a SAP BW/4HANA system.

The BW Modeling API is a sibling of the ADT REST API documented in `adt-protocol-spec.md`. Both share the same CSRF token lifecycle, session management, and discovery protocol. This document covers only BW-specific extensions.

### 1.2 Sources

This specification was reverse-engineered from two independent sources:

1. **SAP BW Modeling Tools for Eclipse Plugin Decompilation** -- 10,981 Java class files across 80 plugin JARs (plugin version 1.27.12). Decompiled using fernflower. The primary source bundle is `com.sap.bw.connectivity` (824 classes), supplemented by model bundles (`model.core`, `model.iobj`, `model.datasource`, `model.dtpa`, `model.datastore`, `model.composite`, `model.trfn`) and the ADT communication layer (`com.sap.adt.communication` v3.56.0).

2. **SAP ADT Communication Framework Decompilation** -- 682 classes providing the shared HTTP infrastructure (CSRF tokens, session management, content negotiation) that BW tools delegate to.

### 1.3 Legal Basis

This specification documents the BW REST protocol for interoperability purposes. Protocol analysis and documentation of undocumented APIs is lawful under interoperability provisions (EU Directive 2009/24/EC Article 6, US DMCA 17 USC 1201(f)). No SAP proprietary source code is copied or reproduced in this specification -- only protocol-level interface information (URLs, headers, XML schemas) is documented.

### 1.4 Conventions

- URL paths are relative to the system base URL (e.g., `https://host:port`)
- `{variable}` denotes a URI template variable (RFC 6570)
- All XML examples use the namespaces defined in Section 3
- "CSRF required" means the request must include a valid `x-csrf-token` header
- See `adt-protocol-spec.md` Section 2 for shared protocol fundamentals (CSRF, sessions, auth)

---

## 2. Protocol Fundamentals

### 2.1 Base URL

All BW Modeling endpoints are rooted under:

```
/sap/bw/modeling/
```

This is distinct from the ADT base URL `/sap/bc/adt/`. The BW modeling API is a separate ICF service tree. However, infrastructure endpoints (CSRF tokens, session management) still use the ADT path at `/sap/bc/adt/`.

### 2.2 Shared Protocol (ADT Layer)

The BW Modeling API shares these mechanisms with the ADT protocol (see `adt-protocol-spec.md`):

- **Authentication**: HTTP Basic or Bearer token
- **CSRF Token Lifecycle**: Fetch via `x-csrf-token: fetch` on GET, use on POST/PUT/DELETE, re-fetch on 403
- **Session Management**: `x-sap-adt-sessiontype: stateful|stateless` header, `sap-contextid` cookie
- **CSRF Fetch Endpoint**: `GET /sap/bc/adt/core/http/sessions` with `x-csrf-token: fetch`
- **Session Close**: `GET /sap/bc/adt/core/http/sessions` with `sap-adt-purpose: close-session`

### 2.3 BW-Specific Headers

The BW layer adds these headers beyond the standard ADT set:

| Header | Direction | Purpose |
|--------|-----------|---------|
| `Transport-Lock-Holder` | Request | Auto-injected on every BW request; identifies the transport request holding the lock |
| `Foreign-Objects` | Request | Object collection for multi-object transport scenarios |
| `Foreign-Object-Locks` | Request | Lock handles for foreign objects in transport |
| `Foreign-Correction-Number` | Request | Transport number for foreign objects |
| `Foreign-Package` | Request | Package for foreign objects |
| `Writing-Enabled` | Response | Indicates whether the system is in write mode |
| `Development-Class` | Response | Package name of the locked object |
| `timestamp` | Response | Server timestamp at lock time |
| `bwmt-level` | Request | BW Modeling Tools capability level (e.g., `50`) |
| `BW_OBJNAME` | Request | Object name (for lock management operations) |
| `BW_TYPE` | Request | Object type (for lock management operations) |

### 2.4 Content Negotiation

BW uses versioned vendor media types for content negotiation. The Accept and Content-Type headers carry version-qualified MIME types:

```
application/vnd.sap.bw.modeling.{component}-v{major}_{minor}_{patch}+xml
```

For example:
```
Accept: application/vnd.sap.bw.modeling.adso-v1_2_0+xml
Content-Type: application/vnd.sap.bw.modeling.trfn-v1_0_0+xml
```

Two vendor prefixes exist (legacy inconsistency):
- `application/vnd.sap.bw.modeling.*+xml` (original, used for object types)
- `application/vnd.sap-bw-modeling.*+xml` (newer, used for services like activation, jobs)

The server advertises supported versions via the ADT discovery document. The client selects the highest mutually-compatible version.

---

## 3. XML Namespace Reference

### 3.1 Primary Namespaces

| Prefix | Namespace URI | Purpose |
|--------|--------------|---------|
| `atom` | `http://www.w3.org/2005/Atom` | Atom Syndication Format (feeds, entries) |
| `bwModel` | `http://www.sap.com/bw/modeling` | Core BW modeling namespace |
| `bwArgs` | `http://www.sap.com/bw/arguments` | BW arguments namespace |
| `bwMsgs` | `http://www.sap.com/bw/messages` | BW messages namespace |

### 3.2 Domain-Specific Namespaces

| Prefix | Namespace URI | Domain |
|--------|--------------|--------|
| `bwObjects` | `http://www.sap.com/bw/objects` | BW Objects container |
| `bwLocks` | `http://www.sap.com/bw/locks` | Lock monitoring |
| `bwActivation` | `http://www.sap.com/bw/massact` | Mass activation |
| `bwCTO` | `http://www.sap.com/bw/cto` | Change Transport Organizer |
| `trCollect` | `http://www.sap.com/bw/trcollect` | Transport collection |
| `bwTransfer` | `http://www.sap.com/bw/transfer` | Object transfer (query components) |
| `bwChaValues` | `http://www.sap.com/bw/chavalues` | Characteristic values |
| `bwVfs` | `http://www.sap.com/bw/virtualFolders` | Virtual folder structure |
| `bwCDSProvs` | `http://www.sap.com/bw/CDSProvs` | CDS view providers |
| `davo` | `http://www.sap.com/bw/dataVolumes` | Data volumes |
| `dds` | `http://www.sap.com/bw/dataSubscriptions` | Data subscriptions |
| `ddscoll` | `http://www.sap.com/bw/dssCollection` | Data subscription collections |
| `ddsscen` | `http://www.sap.com/bw/dssScenario` | Data subscription scenarios |
| `ddsmo` | `http://www.sap.com/bw/dssMassOperation` | Data subscription mass operations |
| `ddsdef` | `http://www.sap.com/bw/dssDefinition` | Data subscription definitions |
| `ddsrefs` | `http://www.sap.com/bw/dssReferences` | Data subscription references |
| `pcs` | `http://www.sap.com/bw/PreCalculatedSet.ecore` | Pre-calculated sets |
| `ddsODP` | `http://www.sap.com/bw/dssODP` | ODP source |
| `exchRate` | `http://www.sap.com/bw/exchangeratetype` | Exchange rate types |
| `lsysInt` | `http://www.sap.com/bw/modeling/lsysint` | Source system integration |
| `dpp` | `http://www.sap.com/bw/dataProtection.ecore` | Data protection |
| `dppFields` | `http://www.sap.com/bw/modeling/dpp/fields` | Data protection fields |

---

## 4. Content-Type Catalog

### 4.1 Object Type Media Types (Versioned)

| Media Type | Object | Default Version | Max Version |
|------------|--------|-----------------|-------------|
| `application/vnd.sap.bw.modeling.query-v{ver}+xml` | Query | `v1_10_0` | `v1_11_0` |
| `application/vnd.sap.bw.modeling.variable-v{ver}+xml` | Variable | `v1_9_0` | `v1_10_0` |
| `application/vnd.sap.bw.modeling.structure-v{ver}+xml` | Structure | `v1_8_0` | `v1_9_0` |
| `application/vnd.sap.bw.modeling.filter-v{ver}+xml` | Filter | `v1_8_0` | `v1_9_0` |
| `application/vnd.sap.bw.modeling.rkf-v{ver}+xml` | Restricted Key Figure | `v1_9_0` | `v1_10_0` |
| `application/vnd.sap.bw.modeling.ckf-v{ver}+xml` | Calculated Key Figure | `v1_9_0` | `v1_10_0` |
| `application/vnd.sap.bw.modeling.dtpa-v1_0_0+xml` | Data Transfer Process | `v1_0_0` | |
| `application/vnd.sap.bw.modeling.trfn-v1_0_0+xml` | Transformation | `v1_0_0` | |
| `application/vnd.sap.bw.modeling.lsys-v1_1_0+xml` | Source System | `v1_1_0` | |
| `application/vnd.sap.bw.modeling.rsds+xml` | DataSource | (unversioned) | |
| `application/vnd.sap.bw.modeling.rsdsint+xml` | DataSource (internal) | (unversioned) | |
| `application/vnd.sap.bw.modeling.cto-v1_1_0+xml` | Transport Organizer | `v1_1_0` | |

### 4.2 Service Media Types

| Media Type | Purpose |
|------------|---------|
| `application/vnd.sap-bw-modeling.massact+xml` | Mass activation request/response |
| `application/vnd.sap-bw-modeling.transfer+xml` | Object transfer |
| `application/vnd.sap-bw-modeling.trcollect+xml` | Transport collection |
| `application/vnd.sap-bw-modeling.bucket+xml` | Bucket/pre-calculated set service |
| `application/vnd.sap.bw.modeling.bwobjects+xml` | BW objects collection |
| `application/vnd.sap.bw.modeling.packages+xml` | Package listing |
| `application/vnd.sap.bw.modeling.pkgsearch+xml` | Package search |
| `application/vnd.sap.bw.modeling.messages+xml` | Messages |
| `application/vnd.sap.bw.modeling.iprovmodel+xml` | InfoProvider model |
| `application/vnd.sap.bw.modeling.arguments+xml` | Arguments |
| `application/vnd.sap.bw.modeling.datacontainer+xml` | Data container |
| `application/vnd.sap.bw.modeling.massopresponse+xml` | Mass operation response |
| `application/vnd.sap.bw.modeling.compmass+xml` | Component mass operations |
| `application/vnd.sap.bw.modeling.virtfolders.v1+xml` | Virtual folders |
| `application/vnd.sap.bw.modeling.virtpath+xml` | Virtual path |
| `application/vnd.sap.bw.modeling.dppfields+xml` | Data protection fields |
| `application/vnd.sap.bw.modeling.dpp.preview-v1_0_0+xml` | Data protection preview |
| `application/vnd.sap.bw.modeling.ratetype+xml` | Exchange rate type |
| `application/vnd.sap.bw.modeling.rulesQueryProperties-v1_0_0+xml` | Query runtime properties |

### 4.3 Job Service Media Types

| Media Type | Purpose |
|------------|---------|
| `application/vnd.sap-bw-modeling.jobs+xml` | All jobs list |
| `application/vnd.sap-bw-modeling.jobs.job+xml` | Single job |
| `application/vnd.sap-bw-modeling.jobs.job.status+xml` | Job status |
| `application/vnd.sap-bw-modeling.jobs.job.progress+xml` | Job progress |
| `application/vnd.sap-bw-modeling.jobs.job.interrupt+xml` | Job interrupt |
| `application/vnd.sap-bw-modeling.jobs.job.cleanup+xml` | Job cleanup |
| `application/vnd.sap-bw-modeling.jobs.steps+xml` | Job steps list |
| `application/vnd.sap-bw-modeling.jobs.step+xml` | Single job step |
| `application/vnd.sap-bw-modeling.balmessages+xml` | BAL log messages |

### 4.4 Reporting and Value Help Media Types

| Media Type | Purpose |
|------------|---------|
| `application/vnd.sap.bw.modeling.bicsrequest-v1_1_0+xml` | BICS reporting request |
| `application/vnd.sap.bw.modeling.bicsresponse-v1_1_0+xml` | BICS reporting response |
| `application/vnd.sap-bw-modeling.isvaluehelp-v1_0_0+xml` | Value help (old) |
| `application/vnd.sap-bw-modeling.valuehelp2-v1_0_0+xml` | Value help v2 |
| `application/vnd.sap-bw-modeling.valuehelp2-v1_1_0+xml` | Value help v2.1 |
| `application/vnd.sap-bw-modeling.chavalues.resp+xml` | Characteristic values response |
| `application/vnd.sap-bw-modeling.chavalues.meta+xml` | Characteristic values metadata |

### 4.5 Data Subscription Media Types

| Media Type | Purpose |
|------------|---------|
| `application/vnd.sap.bw.modeling.ddsdef-v1_0_0+xml` | Data subscription definition |
| `application/vnd.sap.bw.modeling.ddsscenario+xml` | Scenario execution |
| `application/vnd.sap.bw.modeling.ddsscenrefs+xml` | Scenario references |
| `application/vnd.sap.bw.modeling.ddsmo+xml` | Mass operations |
| `application/vnd.sap.bw.modeling.ddssearch+xml` | DSUB search |
| `application/vnd.sap.bw.modeling.ddscollect+xml` | Collection |
| `application/vnd.sap.bw.modeling.ddsvariants+xml` | Variants |
| `application/vnd.sap.bw.modeling.ddssrcodp+xml` | ODP source |
| `application/vnd.sap.bw.modeling.ddsrefs+xml` | DSS references |

---

## 5. BW Discovery

### 5.1 Discovery Endpoint

```
GET /sap/bw/modeling/discovery
Accept: application/atomsvc+xml
```

Returns an Atom Service Document listing all available BW modeling endpoints. Each collection member has:
- A **scheme** URI (e.g., `http://www.sap.com/bw/modeling/adso`)
- A **term** (e.g., `adso`)
- One or more **template links** containing RFC 6570 URI templates

### 5.2 Discovery Scheme-Term Catalog

Each BW service is identified by a `(scheme, term)` pair. The client queries the discovery document to resolve the actual endpoint URL.

#### Object Type Endpoints

| Scheme | Term | Object Type | Template Parameter(s) |
|--------|------|-------------|----------------------|
| `http://www.sap.com/bw/modeling/adso` | `adso` | Advanced DataStore Object | `adsonm` |
| `http://www.sap.com/bw/modeling/hcpr` | `hcpr` | CompositeProvider | `hcprnm` |
| `http://www.sap.com/bw/modeling/iobj` | `iobj` | InfoObject | `infoobject` |
| `http://www.sap.com/bw/modeling/trfn` | `trfn` | Transformation | `trfnnm` |
| `http://www.sap.com/bw/modeling/dtpa` | `dtpa` | Data Transfer Process | `dtpanm` |
| `http://www.sap.com/bw/modeling/rsds` | `rsds` | DataSource | `datasource` + `logsys` |
| `http://www.sap.com/bw/modeling/lsys` | `lsys` | Source System | `sourcesystem` |
| `http://www.sap.com/bw/modeling/query` | `query` | Query | `compid` |
| `http://www.sap.com/bw/modeling/query` | `variable` | Variable | `compid` |
| `http://www.sap.com/bw/modeling/query` | `rkf` | Restricted Key Figure | `compid` |
| `http://www.sap.com/bw/modeling/query` | `ckf` | Calculated Key Figure | `compid` |
| `http://www.sap.com/bw/modeling/query` | `structure` | Structure | `compid` |
| `http://www.sap.com/bw/modeling/query` | `filter` | Filter | `compid` |
| `http://www.sap.com/bw/modeling/fbp` | `fbp` | Open ODS View | `fbpnm` |
| `http://www.sap.com/bw/modeling/alvl` | `alvl` | Aggregation Level | `alvlnm` |
| `http://www.sap.com/bw/modeling/dmod` | `dmod` | DataFlow | `dmodnm` |
| `http://www.sap.com/bw/modeling/dmodcopy` | `dmodcopy` | DataFlow Copy | `dmodnm` |
| `http://www.sap.com/bw/modeling/area` | `area` | InfoArea | `objectname` |
| `http://www.sap.com/bw/modeling/segr` | `segr` | Semantic Group | `segrnm` |
| `http://www.sap.com/bw/modeling/apco` | `apco` | Application Component | `objectname` + `logicalsystem` |
| `http://www.sap.com/bw/modeling/dest` | `dest` | Open Hub Destination | `destnm` |
| `http://www.sap.com/bw/modeling/trcs` | `trcs` | InfoSource | `trcsnm` |
| `http://www.sap.com/bw/modeling/rspc` | `rspc` | Process Chain | `rspcnm` |
| `http://www.sap.com/bw/modeling/doca` | `doca` | Document Store | `docanm` |
| `http://www.sap.com/bw/modeling/dhds` | `dhds` | Dataset | `dhdsnm` |
| `http://www.sap.com/bw/modeling/infoprov` | `infoprovider` | InfoProvider (consumption) | `infoprov` |

#### Planning Endpoints

| Scheme | Term | Object Type | Template Parameter |
|--------|------|-------------|-------------------|
| `http://www.sap.com/bw/modeling/alvl` | `alvl` | Aggregation Level | `alvlnm` |
| `http://www.sap.com/bw/modeling/plcr` | `plcr` | Characteristic Relations | `plcrnm` |
| `http://www.sap.com/bw/modeling/plds` | `plds` | Data Slices | `pldsnm` |
| `http://www.sap.com/bw/modeling/plsq` | `plsq` | Planning Sequence | `plsqnm` |
| `http://www.sap.com/bw/modeling/plse` | `plse` | Planning Function | `plsenm` |
| `http://www.sap.com/bw/modeling/plst` | `plst` | Planning Function Type | `plstnm` |

#### Conversion Type Endpoints

| Scheme | Term | Object Type | Template Parameter |
|--------|------|-------------|-------------------|
| `http://www.sap.com/bw/modeling/ctrt` | `ctrt` | Currency Translation Type | `ctrtnm` |
| `http://www.sap.com/bw/modeling/uomt` | `uomt` | Unit of Measurement Type | `uomtnm` |
| `http://www.sap.com/bw/modeling/thjt` | `thjt` | Key Date Derivation Type | `thjtnm` |

#### Service Endpoints

| Scheme | Term | Service |
|--------|------|---------|
| `http://www.sap.com/bw/modeling/repo` | `bwSearch` | Object search |
| `http://www.sap.com/bw/modeling/repo` | `bwSearchMD` | Search metadata |
| `http://www.sap.com/bw/modeling/repo` | `nodes` | Repository node structure |
| `http://www.sap.com/bw/modeling/repo` | `datasourcenodes` | DataSource nodes |
| `http://www.sap.com/bw/modeling/repo` | `xref` | Cross-references |
| `http://www.sap.com/bw/modeling/repo` | `backendFavorites` | Favorites |
| `http://www.sap.com/bw/modeling/repo` | `virtualfolders` | Virtual folders |
| `http://www.sap.com/bw/modeling/repo` | `datavolumes` | Data volumes |
| `http://www.sap.com/bw/modeling/repo` | `dbInfo` | Database info |
| `http://www.sap.com/bw/modeling/activation` | `activate` | Mass activation |
| `http://www.sap.com/bw/modeling/checkrun` | `check` | Consistency check |
| `http://www.sap.com/bw/modeling/validation` | `validate` | Validation |
| `http://www.sap.com/bw/modeling/jobs` | `jobs` | Background jobs |
| `http://www.sap.com/bw/modeling/cto` | `cto` | Transport organizer |
| `http://www.sap.com/bw/modeling/move_requests` | `move` | Object move |
| `http://www.sap.com/bw/modeling/bucket` | `bucket` | Pre-calculated sets |
| `http://www.sap.com/bw/modeling/characteristic` | `model` | Characteristic model |
| `http://www.sap.com/bw/modeling/keyfigure` | `model` | Key figure model |
| `http://www.sap.com/bw/modeling/rules` | `queryProperties` | Query runtime properties |
| `http://www.sap.com/bw/modeling/utils` | `utils` | Utilities (lock monitoring) |
| `http://www.sap.com/bw/modeling/hana/repository` | `hanaView` | HANA views |
| `http://www.sap.com/bw/modeling/dsub` | `dsub` | Data subscriptions |
| `http://www.sap.com/bw/modeling/comprefactor` | `comprefactor` | Query transfer/refactoring |
| `http://www.sap.com/bw/modeling/dpp/preview` | `preview` | Data protection preview |
| `http://www.sap.com/bw/modeling/dtpa/executerun` | `execute` | DTP execution run |
| `http://www.sap.com/bw/modeling/bwcontent/collection` | `collection` | BW Content collection |
| `http://www.sap.com/bw/modeling/bwcontent/installation` | `installation` | BW Content installation |

---

## 6. BW Object Type Catalog

### 6.1 URL Pattern

All BW modeling objects follow a common URL pattern:

```
/sap/bw/modeling/{tlogo}/{name}/{version}
```

Where:
- `{tlogo}` -- 4-character object type code (lowercase), e.g., `adso`, `iobj`, `hcpr`
- `{name}` -- Object name (uppercase by convention)
- `{version}` -- Version indicator:

| Value | Meaning |
|-------|---------|
| `m` | Modified (inactive/working copy) |
| `a` | Active |
| `d` | Delivery (content version) |

**Special cases:**
- DataSource (RSDS) has 4 segments: `/sap/bw/modeling/rsds/{name}/{sourceSystem}/{version}`
- Application Component (APCO) has 4 segments: `/sap/bw/modeling/apco/{name}/{sourceSystem}/{version}`
- InfoProvider design-time view: append `?view=dt`
- Data preview: `/sap/bw/modeling/comp/datapreview/{techName}`

### 6.2 HTTP Methods

| Method | Purpose | CSRF Required |
|--------|---------|---------------|
| GET | Read object definition | No |
| POST | Create object, lock/unlock, activate | Yes |
| PUT | Update/save object | Yes |
| DELETE | Delete object or favorites | Yes |

### 6.3 Object Types with Discovery

The following table lists all BW object types that support discovery-based endpoint resolution:

| Type Code | Tlogo | Description | InfoProvider | Activation |
|-----------|-------|-------------|:------------:|:----------:|
| ADSO | ADSO | Advanced DataStore Object | Yes | Yes |
| HCPR | HCPR | CompositeProvider | Yes | Yes |
| COPR | COPR | Local CompositeProvider | -- | Yes |
| IOBJ | IOBJ | InfoObject | Yes | Yes |
| TRFN | TRFN | Transformation | -- | Yes |
| DTPA | DTPA | Data Transfer Process | -- | Yes |
| RSDS | RSDS | DataSource | -- | Yes |
| TRCS | TRCS | InfoSource | -- | Yes |
| DMOD | DMOD | DataFlow | -- | Yes |
| DEST | DEST | Open Hub Destination | -- | Yes |
| CUBE | CUBE | InfoCube | Yes | Yes |
| ODSO | ODSO | DataStore Object (classic) | Yes | Yes |
| MPRO | MPRO | MultiProvider | Yes | Yes |
| FBPA | FBPA | Open ODS View | Yes | Yes |
| HYBR | HYBR | HybridProvider | Yes | Yes |
| ALVL | ALVL | Aggregation Level | Yes | Yes |
| AREA | AREA | InfoArea | -- | -- |
| LSYS | LSYS | Source System | -- | -- |
| SEGR | SEGR | Semantic Group | -- | -- |
| APCO | APCO | Application Component | -- | -- |
| RSPC | RSPC | Process Chain | -- | -- |
| DOCA | DOCA | Document Store | -- | Yes |
| DHDS | DHDS | Dataset | -- | -- |
| ROUT | ROUT | Transfer Routine | -- | Yes |
| PLCR | PLCR | Characteristic Relations | -- | -- |
| PLDS | PLDS | Data Slices | -- | -- |
| PLSQ | PLSQ | Planning Sequence | -- | -- |
| PLSE | PLSE | Planning Function | -- | -- |
| PLST | PLST | Planning Function Type | -- | -- |
| CTRT | CTRT | Currency Translation Type | -- | -- |
| UOMT | UOMT | Unit of Measurement Type | -- | -- |
| THJT | THJT | Key Date Derivation Type | -- | -- |

### 6.4 Tlogo Delivery Names (Transport)

Each Tlogo has a 4-character delivery name used in CTS transport objects:

| Tlogo | Delivery Name | Tlogo | Delivery Name |
|-------|:------------:|-------|:------------:|
| ADSO | DDSO | MPRO | DMPR |
| ALVL | DALV | ODSO | DODS |
| CUBE | DCUB | RSDS | SHDS |
| COPR | DCOP | TRCS | DTRC |
| DEST | DDES | TRFN | DTRF |
| DMOD | DDMO | DOCA | DDOC |
| DTPA | DTPD | ROUT | DROU |
| FBPA | FBPD | HCPR | DHCP |
| HYBR | DHYB | IOBJ | DIOB |

### 6.5 Query Component Subtypes

Query-related objects all use the ELEM Tlogo but are distinguished by subtype:

| Subtype | Code | Type Code |
|---------|------|-----------|
| Report/Query | `REP` | `ELEM` |
| Filter | `SOB` | `SOB` |
| Restricted Key Figure | `RKF` | `RKF` |
| Calculated Key Figure | `CKF` | `CKF` |
| Variable | `VAR` | `VAR` |
| Structure | `STR` | `STR` |

---

## 7. Object Search

### 7.1 Search Endpoint

Resolved via discovery: scheme `http://www.sap.com/bw/modeling/repo`, term `bwSearch`, template link `http://www.sap.com/bw/modeling/repo/is/bwsearch`.

```
GET /sap/bw/modeling/is/bwsearch?searchTerm={term}&maxSize={max}&objectType={type}
Accept: application/atom+xml
```

### 7.2 URI Template Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `searchTerm` | string | Search pattern (supports `*` wildcard) |
| `maxSize` | integer | Maximum number of results |
| `objectType` | string | Filter by object type code (e.g., `ADSO`, `IOBJ`) |
| `objectSubType` | string | Filter by subtype |
| `objectVersion` | string | Filter by version (`A`, `M`, `N`) |
| `objectStatus` | string | Filter by status (`ACT`, `INA`, `OFF`) |
| `changedBy` | string | Filter by last changed user |
| `changedOnFrom` | string | Filter: changed on or after date |
| `changedOnTo` | string | Filter: changed on or before date |
| `createdBy` | string | Filter by creator |
| `createdOnFrom` | string | Filter: created on or after date |
| `createdOnTo` | string | Filter: created on or before date |
| `dependsOnObjectName` | string | Filter: objects depending on this name |
| `dependsOnObjectType` | string | Filter: objects depending on this type |
| `searchInDescription` | boolean | Search in object descriptions |
| `searchInName` | boolean | Search in object names |

### 7.3 Response

Returns an Atom feed with entries. The feed element may include:
- `bwModel:feedIncomplete` attribute -- indicates truncated results (more objects exist than `maxSize`)

Each entry contains:
- `atom:title` -- object description
- `atom:id` -- object URI
- `atom:content` -- XML with object attributes (`objectName`, `objectType`, `objectSubtype`, `objectVersion`, `objectStatus`, `objectDesc`, `lastChangedAt`, `technicalObjectName`, `displayObjectName`)
- `atom:link` -- with `rel="self"` pointing to the object URI

---

## 8. Object Lifecycle Operations

### 8.1 Read Object

```
GET /sap/bw/modeling/{tlogo}/{name}/{version}
Accept: application/vnd.sap.bw.modeling.{tlogo}-v{version}+xml
```

Response: Object definition XML in the type-specific format.

### 8.2 Lock Object

```
POST /sap/bw/modeling/{tlogo}/{name}?action=lock
x-csrf-token: {token}
x-sap-adt-sessiontype: stateful
```

Optional headers:
- `activity_context` -- Activity code (`CHAN` for change, `DELE` for delete, `MAIN` for maintain)
- `parent_name` -- Parent object name (for child object locking)
- `parent_type` -- Parent object type

Response XML (flat elements, no root wrapper, no namespace):
```xml
<LOCK_HANDLE>{handle}</LOCK_HANDLE>
<CORRNR>{transport_request}</CORRNR>
<CORRTEXT>{request_description}</CORRTEXT>
<CORRUSER>{request_owner}</CORRUSER>
<IS_LOCAL>{X_if_local}</IS_LOCAL>
<IS_LINK_UP>{X_if_foreign}</IS_LINK_UP>
```

**Note:** The lock response is unusual -- it returns flat XML elements with no root element and no namespace declaration. Parsers should handle this as a fragment.

Response headers:
- `timestamp` -- Server timestamp
- `Development-Class` -- Package name
- `Foreign-Object-Locks` -- Lock handles for associated objects

### 8.3 Save Object

```
PUT /sap/bw/modeling/{tlogo}/{name}?lockHandle={handle}&corrNr={transport}&timestamp={ts}
x-csrf-token: {token}
x-sap-adt-sessiontype: stateful
Content-Type: application/vnd.sap.bw.modeling.{tlogo}-v{version}+xml
```

Request body: Modified object definition XML.

### 8.4 Unlock Object

```
POST /sap/bw/modeling/{tlogo}/{name}?action=unlock
x-csrf-token: {token}
x-sap-adt-sessiontype: stateful
```

### 8.5 Delete Object

```
DELETE /sap/bw/modeling/{tlogo}/{name}?lockHandle={handle}&corrNr={transport}
x-csrf-token: {token}
```

### 8.6 Complete Edit Workflow

```
1. LOCK:     POST /sap/bw/modeling/{tlogo}/{name}?action=lock
             -> Returns LOCK_HANDLE, CORRNR, timestamp, Development-Class
             -> Stateful session required

2. MODIFY:   PUT /sap/bw/modeling/{tlogo}/{name}?lockHandle={handle}&corrNr={transport}&timestamp={ts}
             -> Sends modified content
             -> Same stateful session

3. ACTIVATE: POST {activation_endpoint}?mode=activate&corrnum={transport}
             -> Content-Type: application/vnd.sap-bw-modeling.massact+xml
             -> Body: bwActivation:objects XML with object list
             -> Can be synchronous or background (asjob=true)
             -> Stateless session (activation is independent of edit session)

4. UNLOCK:   POST /sap/bw/modeling/{tlogo}/{name}?action=unlock
             -> Stateful session (same as lock)

5. CLOSE:    GET /sap/bc/adt/core/http/sessions
             -> sap-adt-purpose: close-session
             -> Terminates the ABAP stateful session
```

---

## 9. Activation

### 9.1 Discovery

Resolved via discovery: scheme `http://www.sap.com/bw/modeling/activation`, term `activate`, template relation `massact`.

### 9.2 Validate (Pre-Activation Check)

```
POST {activation_endpoint}?mode=validate&sort={bool}&onlyina={bool}
Content-Type: application/vnd.sap-bw-modeling.massact+xml
x-csrf-token: {token}
```

Request body:
```xml
<bwActivation:objects xmlns:bwActivation="http://www.sap.com/bw/massact"
                      bwChangeable="" basisChangeable="">
  <object objectName="ZADSO001" objectType="ADSO" objectVersion="M"
          technicalObjectName="ZADSO001" objectSubtype=""
          objectDesc="My DataStore" objectStatus="INA"
          lastChangedAt="20260214120000" activateObj="true"
          associationType="" corrnum="" package="ZTEST"
          href="/sap/bw/modeling/adso/ZADSO001/m" hrefType=""/>
</bwActivation:objects>
```

Root element attributes:
- `bwChangeable` -- whether BW objects are changeable in this system
- `basisChangeable` -- whether Basis objects are changeable

Per-object attributes:
- `objectName`, `technicalObjectName` -- display and technical names
- `objectType`, `objectSubtype`, `objectVersion`, `objectStatus`
- `objectDesc`, `lastChangedAt`, `activateObj` (boolean)
- `associationType` -- relationship to other objects in the collection
- `corrnum` -- transport request number
- `package` -- ABAP package
- `href` -- object URI, `hrefType` -- URI type hint

Response: Same XML format enriched with validation results and per-object messages.

### 9.3 Activate

```
POST {activation_endpoint}?mode=activate&simu={bool}&corrnum={transport}
Content-Type: application/vnd.sap-bw-modeling.massact+xml
x-csrf-token: {token}
```

Root element attributes:
- `forceAct` -- Force activation even with warnings
- `execChk` -- Execute consistency checks during activation
- `withCTO` -- Transport integration active

Response: `BwMassOperationResult` with activation status and per-object messages.

### 9.4 Background Activation

```
POST {activation_endpoint}?mode=activate&asjob=true&corrnum={transport}
Content-Type: application/vnd.sap-bw-modeling.massact+xml
x-csrf-token: {token}
```

Returns a job reference. Poll for completion using the Job Service (Section 13).

### 9.5 Activation Modes

| Mode | URI Param | Description |
|------|-----------|-------------|
| Validate | `mode=validate` | Pre-check without activating |
| Activate | `mode=activate&simu=false` | Activate immediately |
| Simulate | `mode=activate&simu=true` | Dry run of activation |
| Background | `mode=activate&asjob=true` | Activate as background job |

---

## 10. Locking

### 10.1 Object Locking Protocol

See Section 8.2 and 8.4 for lock/unlock operations.

### 10.2 Activity Constants

| Activity | Key | Activity Code |
|----------|-----|:------------:|
| Create | `CREA` | -- |
| Change | `CHAN` | `23` |
| Display | `DISP` | -- |
| Delete | `DELE` | `06` |
| Maintain | `MAIN` | `23` |
| Activate | `ACTV` | -- |
| Execute | `EXCT` | -- |
| Change Config | `CCFG` | -- |

### 10.3 Lock Monitoring

Resolved via discovery: scheme `http://www.sap.com/bw/modeling/utils`, term `utils`, relation `locks`.

```
GET {locks_endpoint}?user={user}&search={pattern}&bwselect={mode}&resultsize={max}
```

Response XML:
```xml
<bwLocks:dataContainer xmlns:bwLocks="http://www.sap.com/bw/locks">
  <lock client="001" user="DEVELOPER" mode="E"
        tableName="RSBWOBJ_ENQUEUE" tableDesc="Lock Table"
        object="ZADSO001" arg="{base64}" owner1="{base64}"
        owner2="{base64}" timestamp="20260214120000"
        updCount="1" diaCount="0"/>
</bwLocks:dataContainer>
```

### 10.4 Delete a Lock

```
DELETE {locks_endpoint}?user={user}
x-csrf-token: {token}
BW_OBJNAME: {tableName}
BW_ARGUMENT: {base64_argument}
BW_SCOPE: 1
BW_TYPE: {mode}
BW_OWNER1: {base64_owner1}
BW_OWNER2: {base64_owner2}
```

---

## 11. Transport

### 11.1 Transport Organizer (CTO)

Resolved via discovery: scheme `http://www.sap.com/bw/modeling/cto`, term `cto`.

### 11.2 Check Transport State

```
GET {cto_endpoint}?rddetails={off|objs|all}&rdprops={bool}&ownonly={bool}&allmsgs={bool}
Accept: application/vnd.sap.bw.modeling.cto-v1_1_0+xml
```

Response headers:
- `Writing-Enabled` -- Whether write mode is active
- `versionLevel` -- Content version level

Response XML:
```xml
<bwCTO:transport xmlns:bwCTO="http://www.sap.com/bw/cto">
  <properties>...</properties>
  <changeability>
    <setting name="ADSO" transportable="true" impVersion="" changeable="X" tlogo="ADSO"/>
  </changeability>
  <requests>
    <request number="NPLK900001" functionType="K" status="D" description="...">
      <task number="NPLK900002" functionType="S" status="D" owner="DEVELOPER"/>
    </request>
  </requests>
  <objects>
    <object name="ZADSO001" type="ADSO" operation="I" uri="/sap/bw/modeling/adso/ZADSO001">
      <lock>
        <request number="NPLK900001">
          <task number="NPLK900002"/>
        </request>
      </lock>
      <tadir status="..."/>
    </object>
  </objects>
  <messages>...</messages>
</bwCTO:transport>
```

### 11.3 Validate for Transport

```
POST {cto_endpoint} (using relation: check)
Content-Type: application/vnd.sap.bw.modeling.cto-v1_1_0+xml
x-csrf-token: {token}
```

Request body: `bwCTO:transport` with `objects` list.

### 11.4 Write to Transport

```
POST {cto_endpoint}?corrnum={transport}&package={pkg}&simulate={bool}&allmsgs={bool}
Content-Type: application/vnd.sap.bw.modeling.cto-v1_1_0+xml
x-csrf-token: {token}
```

Uses the `write` relation from discovery.

### 11.5 Transport Request Function Types

| Key | Description | Changeable |
|-----|-------------|:----------:|
| `K` | Workbench Request | -- |
| `W` | Customizing Request | -- |
| `S` | Development/Correction Task | -- |
| `R` | Repair Task | -- |
| `Q` | Customizing Task | -- |
| `T` | Transport of Copies | -- |
| `C` | Relocation without Package Change | -- |
| `O` | Relocation with Package Change | -- |
| `E` | Relocation of Complete Package | -- |
| `M` | Client Transport Request | -- |
| `L` | Deletion Transport | -- |
| `X` | Unclassified Task | -- |
| `G` | CTS Project | -- |
| `P` | Upgrade | -- |
| `D` | Support Package | -- |
| `F` | Piece List | -- |

### 11.6 Transport Request Status Types

| Key | Description | Changeable |
|-----|-------------|:----------:|
| `D` | Modifiable | Yes |
| `L` | Modifiable (Protected) | Yes |
| `O` | Release Started | No |
| `R` | Released | No |
| `N` | Released (with Import Protection) | No |
| `P` | Release Preparation | No |

### 11.7 Transport Collection Grouping Modes

| Key | Description |
|-----|-------------|
| `000` | Only Necessary |
| `001` | Complete Scenario |
| `002` | Backup Transport |
| `003` | Dataflow Above |
| `033` | Dataflow Above (Direct) |
| `004` | Dataflow Below |
| `005` | Metadata Upload |

---

## 12. System Information Endpoints

Resolved via discovery under scheme `http://www.sap.com/bw/modeling/repo`.

### 12.1 Database Info

```
GET /sap/bw/modeling/repo/is/dbinfo
Accept: application/atom+xml
```

Returns HANA host, port, schema information.

### 12.2 System Info

```
GET /sap/bw/modeling/repo/is/systeminfo
Accept: application/atom+xml
```

Returns system properties.

### 12.3 Changeability Settings

```
GET /sap/bw/modeling/repo/is/chginfo
Accept: application/atom+xml
```

Returns per-TLOGO changeability settings.

### 12.4 ADT URI Mappings

```
GET /sap/bw/modeling/repo/is/adturi
Accept: application/atom+xml
```

Returns mappings between BW URIs and ADT URIs (table definitions, data element definitions, view definitions).

### 12.5 Value Help

```
GET /sap/bw/modeling/is/values/infoareas?maxrows=100
```

Returns InfoArea tree for value help. Other value help endpoints follow similar patterns under `/sap/bw/modeling/is/values/`.

---

## 13. Background Job Service

### 13.1 Job URL Pattern

```
/sap/bw/modeling/jobs/{25-character-GUID}
```

The GUID matches pattern `[A-Z0-9]{25}`.

### 13.2 Job Status Polling

```
GET {job_url}/status
Accept: application/vnd.sap-bw-modeling.jobs.job.status+xml
```

### 13.3 Job Progress

```
GET {job_url}/progress
Accept: application/vnd.sap-bw-modeling.jobs.job.progress+xml
```

### 13.4 Job Sub-Resources

Jobs expose sub-resources via link relations:

| Relation | Media Type | Purpose |
|----------|-----------|---------|
| `status` | `application/vnd.sap-bw-modeling.jobs.job.status+xml` | Current status |
| `progress` | `application/vnd.sap-bw-modeling.jobs.job.progress+xml` | Progress percentage |
| `result` | `application/vnd.sap-bw-modeling.jobs.job+xml` | Final result |
| `steps` | `application/vnd.sap-bw-modeling.jobs.steps+xml` | Step list |
| `step` | `application/vnd.sap-bw-modeling.jobs.step+xml` | Individual step |
| `messages` | `application/atom+xml` | Messages (Atom) |
| `messages` | `application/vnd.sap-bw-modeling.balmessages+xml` | BAL log messages |
| `interrupt` | `application/vnd.sap-bw-modeling.jobs.job.interrupt+xml` | Interrupt/cancel |
| `restart` | -- | Restart failed job |
| `cleanup` | `application/vnd.sap-bw-modeling.jobs.job.cleanup+xml` | Cleanup resources |

### 13.5 Job Status Values

| Key | Description |
|-----|-------------|
| `N` | New (not started) |
| `R` | Running |
| `E` | Error |
| `W` | Warning |
| `S` | Success |
| `NA` | Not Applicable |

### 13.6 Job Types

| Key | Description |
|-----|-------------|
| `DTO` | Data Transfer Object |
| `REMODELING` | Remodeling |
| `DATAFLOWCOPY` | Dataflow Copy |
| `DTP_LOAD` | DTP Execution |
| `DTP_SIMULATION` | DTP Simulation |
| `TLOGO_ACTIVATION` | Object Activation |
| `CONTENTACTIVATION` | Content Activation |
| `DATASOURCE_REPLICATION` | DataSource Replication |
| `CUSTOMIZING_TRANSFER` | Transfer Customizing Settings |
| `EXCHANGE_RATE_TRANSFER` | Transfer Exchange Rates |
| `SOURCE_SYSTEM_CONTENT_UPDATE` | Update Source System Content |
| `DATA_PRODUCT` | Data Product |

---

## 14. Link Relations

BW uses a rich set of link relations prefixed with `http://www.sap.com/bw/modeling/relations:`.

### 14.1 Core Relations

| Relation (short form) | Purpose |
|-----------------------|---------|
| `:lock` | Lock object |
| `:unlock` | Unlock object |
| `:exists` | Check existence |
| `:activeVersion` | Active version link |
| `:deliveryVersion` | Delivery version link |
| `:historyVersion` | History version link |
| `:comparableVersion` | Comparable version link |
| `:model` | Model content |
| `:metaData` | Metadata |
| `:iprovmodel` | InfoProvider model |
| `:children` | Child nodes |
| `:child` | Single child |
| `:root` | Root node |
| `:tree` | Tree structure |
| `:xref` | Cross-references |

### 14.2 InfoObject Relations

| Relation | Purpose |
|----------|---------|
| `:characteristics` | Characteristics list |
| `:infoObjects` | InfoObjects list |
| `:infoProviders` | InfoProviders list |
| `:infoAreas` | InfoAreas list |
| `:characteristicValues` | Characteristic values |
| `:characteristicHierarchies` | Hierarchies |
| `:characteristicVariables` | Variables |

### 14.3 Replication Relations

| Relation | Purpose |
|----------|---------|
| `:replication` | Single object replication |
| `:compreplication` | Component replication |
| `:massreplication` | Mass replication |
| `:inactivate` | Inactivate object |
| `:contentUpdate` | Content update |

### 14.4 HANA Relations

| Relation | Purpose |
|----------|---------|
| `:sapHANAView` | HANA view |
| `:sapHANAViewAttribute` | HANA view attribute |
| `:sapHANAPackage` | HANA package |
| `:hanaentity` | HANA entity |
| `:dbSourceSystems` | Database source systems |
| `:dbObjectName` | Database object name |
| `:dbObjectSchemes` | Database object schemes |
| `:hanaRemoteSources` | HANA remote sources |
| `:hanaRemoteDatabases` | HANA remote databases |

---

## 15. Status and Version Constants

### 15.1 Object Version

| Key | Description |
|-----|-------------|
| `A` | Active |
| `M` | Modified (inactive) |
| `N` | New |
| `B` | Backup |
| `D` | Content (delivery) |
| `H` | Historic |
| `T` | Transport |

### 15.2 Object Status

| Key | Description |
|-----|-------------|
| `ACT` | Active, executable |
| `INA` | Inactive, not executable |
| `OFF` | Switched off |
| `PRO` | Production |

### 15.3 Change Status

| Key | Description |
|-----|-------------|
| `ADDED` | Added |
| `CHANGED` | Changed |
| `DELETED` | Deleted |
| `ORIGINAL` | Original |

### 15.4 Changeability Type

| Key | Description |
|-----|-------------|
| (empty) | Not Changeable |
| `X` | Original Changeable |
| `A` | All Changeable |

### 15.5 XML Message Severity

| Key | Description |
|-----|-------------|
| `I` | Information |
| `S` | Success |
| `W` | Warning |
| `E` | Error |
| `X` | Exception/Abort |

### 15.6 ABAP Boolean

The standard SAP boolean value is `X` for true, empty string for false.

---

## 16. Object Association Types

BW tracks relationships between objects using association type codes:

| Code | Description |
|------|-------------|
| `000` | Default |
| `001` | Existentially requires |
| `002` | Requires |
| `003` | Used by |
| `004` | Sends data to |
| `005` | Receives data from |
| `006` | Is displayed in |
| `020` | Schedules |
| `021` | Is scheduled by |
| `100` | Is compounded to |
| `101` | Has attribute |
| `102` | Has navigation attribute |
| `103` | Has aggregation-referenced characteristic |
| `104` | Refers to characteristic |
| `105` | Has unit |
| `106` | Has access/non-cumulative value change |
| `107` | Has outflow |
| `108` | Has fixed key figure |
| `109` | Referenced key figure |
| `110` | Can be time-converted into |
| `111` | Has as elimination characteristic |
| `112` | Has navigation attribute as InfoProvider |
| `113` | Has as hierarchy characteristic |
| `114` | For determination of source currency |
| `115` | For determination of target currency |
| `116` | For time reference in currency translation type |
| `117` | For exchange rate determination in currency translation type |
| `118` | Quantity translation: determining translation factor |
| `119` | Quantity translation: reference InfoObject |
| `120` | Quantity translation: determine source quantity unit |
| `121` | Quantity translation: determine target quantity unit |
| `122` | Quantity translation: determine quantity unit (attribute) |
| `123` | Quantity translation: determine quantity unit (attribute, alt.) |
| `124` | Has XXL attribute |
| `999` | Undefined |

---

## 17. ODP (Operational Data Provisioning) Suffixes

ODP objects use suffixes to distinguish sub-entities:

| Suffix | Description |
|--------|-------------|
| `$F` | Facts |
| `$D` | Document Store |
| `$M` | Master Data View |
| `$E` | Extraction |
| `$V` | Value Help |
| `$W` | View |
| `$P` | Time-independent Attributes |
| `$Q` | Time-dependent Attributes |
| `$T` | Texts |
| `$H` | Hierarchy |
| `$1` | Hierarchy Header |
| `$2` | Hierarchy Header Text |
| `$3` | Hierarchy Node |
| `$4` | Hierarchy Node Text |
| `$U` | Hierarchy Union |
| `$N` | Derivation Function |

---

## 18. ABAP Dictionary Data Types

BW uses standard ABAP dictionary types for field definitions:

| Type | Description |
|------|-------------|
| `CHAR` | Character |
| `CLNT` | Client |
| `CUKY` | Currency Key |
| `CURR` | Currency Amount |
| `DATS` | Date (YYYYMMDD) |
| `DEC` | Packed Decimal |
| `FLTP` | Floating Point |
| `INT1` | 1-byte Integer |
| `INT2` | 2-byte Integer |
| `INT4` | 4-byte Integer |
| `INT8` | 8-byte Integer |
| `LANG` | Language |
| `LCHR` | Long Character String |
| `LRAW` | Long Byte String |
| `NUMC` | Numeric Character |
| `QUAN` | Quantity |
| `RAW` | Raw Binary |
| `RSTR` | Byte String (BLOB) |
| `STRG` | String |
| `SSTR` | Short String |
| `TIMS` | Time (HHMMSS) |
| `UNIT` | Unit of Measurement |
| `ACCP` | Accounting Period |

---

## 19. BW/4HANA Mode

SAP BW/4HANA systems operate in one of three modes:

| Mode | Description |
|------|-------------|
| `STANDARD` | Standard mode |
| `PREPARE` | Preparation mode (pre-migration) |
| `STRICT` | Strict mode (BW/4HANA only, deprecated types disabled) |

---

## 20. Architecture Notes

### 20.1 BW Wraps ADT (Delegation Pattern)

The BW plugin does NOT extend the ADT REST framework -- it wraps it. `BwRestResource` is a pure delegate around `IRestResource` from `com.sap.adt.communication`. This means:

- All CSRF, session, auth, and load-balancer handling is invisible to BW code
- BW only chooses between stateless and enqueue (stateful) sessions
- Content handlers are registered per-request via `addContentHandler()`

### 20.2 Discovery-Driven URL Resolution

The BW plugin does NOT hardcode endpoint URLs. Instead:

1. `GET /sap/bw/modeling/discovery` returns an Atom service document
2. Each resource type has a `(scheme, term)` pair identifying it
3. The collection member provides a URI template (RFC 6570)
4. Templates contain variables like `{objname}`, `{srcSysName}`, etc.
5. The client expands the template with actual values

Hardcoded URLs exist only as fallbacks for legacy code paths.

### 20.3 Content Handler Architecture

BW uses two XML parsing approaches:

1. **SAP Atom Parser** (higher-level): `AbstractAtomFeedContentHandler` uses `SAtomParserFactory.parseFeedDocument()`. Used for search results, node structures, value help.

2. **StAX Direct Parsing** (lower-level): `BasisParser` wraps `XMLStreamReader`. Used for transport organizer, activation, locking, transfer objects.

Content negotiation is filter-driven. `CompatibilityResourceFilter` injects `Accept`/`Content-Type` headers based on versioned vendor media types negotiated against the discovery document.

---

## 21. Per-Object-Type XML Schemas

This section documents the XML body structure for GET/PUT of each major BW object type, extracted from the EMF model definitions in the decompiled Eclipse plugins.

### 21.1 Common EMF Serialization Pattern

All BW object types use Eclipse Modeling Framework (EMF) for XML serialization. The common pattern:
- Root element is a `DocumentRoot` wrapper (mixed content)
- The actual object is a child element of `DocumentRoot`
- Namespace prefixes declared on root
- Elements use EMF ExtendedMetaData annotations for XML mapping (`kind=element` vs `kind=attribute`)
- Resource URI scheme: `outputstream://localhost/temp.{ext}`
- Load option: `RECORD_UNKNOWN_FEATURE=true` (forward compatibility)

### 21.2 ADSO (Advanced DataStore Object)

**EMF Namespace:** `http://www.sap.com/bw/modeling/BwAdso.ecore`
**File Extension:** `.adso`
**Root Element:** `<dataStore>` (within `<DocumentRoot>`)
**Content Handler:** EMF ResourceSet deserialization (`com.sap.bw.model.adso`)

**Root `<dataStore>` Attributes:**
| Attribute | Type | Description |
|-----------|------|-------------|
| `name` | String | Technical name (unique key) |
| `description` | String | Long description |
| `version` | String | Model version |
| `activateData` | Boolean | Auto-activate data on load |
| `autorefresh` | Boolean | Auto-refresh flag |
| `checkDeltaConsistency` | Boolean | Delta consistency check |
| `coldConnectionName` | String | Cold store connection |
| `compatibilityViews` | Boolean | Generate compatibility views |
| `conversionNecessary` | Boolean | Conversion needed flag |
| `cubeDeltaOnly` | Boolean | Delta-only cube |
| `directUpdate` | Boolean | Allow direct update |
| `enableSqlAccess` | Boolean | SQL access enabled |
| `externalHanaView` | Boolean | External HANA view |
| `modelType` | Enum | Model type identifier |
| `writeInterface` | Enum | Write interface mode |
| `planningEnabled` | Boolean | Planning support |
| `snapshotMode` | Boolean | Snapshot mode |
| `temporaryStorage` | Boolean | Temporary storage |

**Child Elements:**
| Element | Type | Cardinality | Description |
|---------|------|-------------|-------------|
| `tlogoProperties` | BwEntityProperties | 1..1 | Entity metadata (package, author, timestamps) |
| `dataStoreIndexes` | DataStoreIndex | 0..* | Secondary indexes |
| `validityElements` | BwElement | 0..* | Validity period fields |
| `hashElements` | BwElement | 0..* | Hash check fields |
| `partitioningcriteria` | Reference | 0..* | Partitioning criteria |
| `tables` | Reference | 0..* | Physical table references |
| `anonymizationRule` | Reference | 0..* | Data protection rules |
| `chaConst` | Reference | 0..* | Characteristic constants |
| `semanticGroup` | SemanticGroup | 0..1 | Semantic grouping |
| `referenceSemanticGroups` | Reference | 0..* | Referenced semantic groups |
| `remodelingElements` | Reference | 0..* | Remodeling definitions |
| `template` | Template | 0..1 | Template reference |
| `templateURI` | Reference | 0..* | Template URIs |
| `oDataProperties` | ODataProperties | 0..1 | OData exposure properties |

### 21.3 IOBJ (InfoObject)

**EMF Namespace:** `http://www.sap.com/bw/modeling/BwIobj.ecore`
**File Extension:** `.iobj`
**Root Element:** `<InfoObject>` (within `<DocumentRoot>`)
**Content Handler:** `InfoObjectContentHandler` -- deserialize only (serialize throws `UnsupportedOperationException`)
**Content-Type:** `application/xml`

**Root `<InfoObject>` Attributes:**
| Attribute | Type | Description |
|-----------|------|-------------|
| `name` | String | InfoObject technical name (key) |
| `infoObjectType` | Enum | `CHARACTERISTIC`, `KEYFIGURE`, `TIME`, `UNIT` |
| `version` | String | Version identifier |
| `description` | String | Long description |
| `shortDescription` | String | Short description |
| `fieldName` | String | ABAP field name |
| `lastChangedBy` | String | Last modifier |
| `lastChangedOn` | DateTime | Last modification timestamp |
| `attributeOnly` | Boolean | Attribute-only InfoObject |
| `metaObject` | Boolean | Meta object flag |
| `readAccessLoggingEnabled` | Boolean | RAL enabled |
| `technicalInfoObject` | Boolean | Technical InfoObject |
| `baseInfoObjectName` | String | Base InfoObject (for derived) |

**Characteristic-Specific Attributes (extends InfoObject):**
| Attribute | Type | Description |
|-----------|------|-------------|
| `objectSpecificDataType` | Enum | `CHARACTER_STRING`, `NUMERICAL_TEXT`, `DATE`, `TIME`, `CURRENCY_KEY`, `LANGUAGE`, `UNIT`, `RAW` |
| `semantics` | Enum | Semantic classification |
| `uuid` | Boolean | UUID-based characteristic |

**Keyfigure-Specific Attributes (extends InfoObject):**
| Attribute | Type | Description |
|-----------|------|-------------|
| `semantics` | Enum | Keyfigure data type semantics |
| `length` | Integer | Data length |
| `precision` | Short | Decimal precision |
| `scale` | Short | Decimal scale |
| `highPrecision` | Boolean | High precision mode |
| `aggregationType` | Enum | Aggregation type |

**Child Elements:**
| Element | Type | Cardinality | Description |
|---------|------|-------------|-------------|
| `tlogoProperties` | BwEntityProperties | 1..1 | Entity metadata |
| `dataElement` | DDICElement | 1..1 | DDIC data element |
| `texts` | Texts | 0..* | Multilingual descriptions |
| `compoundInfoObjects` | String | 0..* | Compound InfoObject references |
| `referencedInfoObject` | Reference | 1..1 | Referenced InfoObject |
| `ralDomainBusinessArea` | Reference | 0..* | RAL domains |
| `consumptionViewProperties` | Reference | 0..1 | Consumption view config |
| `xxlAttributes` | XXLAttribute | 0..* | Extended attributes (Characteristic only) |
| `anonymizationRule` | Reference | 0..* | Data protection rules |

### 21.4 TRFN (Transformation)

**EMF Namespace:** `http://www.sap.com/bw/modeling/Trfn.ecore`
**File Extension:** `.trfn`
**Root Element:** `<transformation>` (within `<DocumentRoot>`)

**Root `<transformation>` Attributes:**
| Attribute | Type | Description |
|-----------|------|-------------|
| `name` | String | Transformation name |
| `description` | String | Description |
| `abapProgram` | String | ABAP program name |
| `classNameA` | String | ABAP class name |
| `ABAPEndroutineDisabled` | Boolean | Disable ABAP end routine |
| `allowCurrencyAndUnitConversion` | Boolean | Allow currency/unit conversion |
| `checkUnitsForConsistency` | Boolean | Check unit consistency |
| `enableCurrencyAndUnitConversion` | Boolean | Enable currency/unit conversion |
| `enableErrorHandlingInRoutines` | Boolean | Enable error handling |
| `enableInverseRoutines` | Boolean | Enable inverse routines |
| `endRoutine` | String | End routine code |
| `expertRoutine` | String | Expert routine code |
| `startRoutine` | String | Start routine code |
| `hapProgram` | String | HANA program |
| `HANARuntime` | Boolean | Use HANA runtime |
| `initializeNullValues` | Boolean | Initialize nulls |
| `sapHANAExecutionPossible` | Enum | `MUST_BE`, `MUST_NOT`, `COULD`, `INVALID` |
| `unsupportedTransformation` | Boolean | Unsupported flag |

**Child Elements:**
| Element | Type | Cardinality | Description |
|---------|------|-------------|-------------|
| `tlogoProperties` | BwEntityProperties | 1..1 | Entity metadata |
| `source` | Structure | 1..1 | Source structure |
| `target` | Structure | 1..1 | Target structure |
| `group` | Group | 0..* | Transformation rule groups |

**Structure Element Attributes:** `name`, `description`, `id` (int), `type`, `subType`, `objectStatus`
**Structure Child:** `<segment>` (0..*) containing `<element>` (TransformationElement) entries

**TransformationElement Attributes:** `posit`, `intType`, `aggregationType`, `defaultAggregationType`, `key` (boolean), `transferRoutine`, `performMasterDataCheck` (boolean), `masterDataCheckObjectName`, `masterDataCheckObjectType`

**Group Attributes:** `id` (int), `description`, `type` (enum: `STANDARD`, `TECHNICAL`, `NEW`, `GLOBAL`)
**Group Child:** `<rule>` (0..*) containing `<source>`, `<target>`, `<step>` elements

**Step Element Types (polymorphic, determined by type attribute):**

| Step Type | Key Attributes |
|-----------|---------------|
| `StepDirect` | (none -- direct mapping) |
| `StepConstant` | `constant` |
| `StepFormula` | `formula` |
| `StepConversion` | `conversionTlogo`, `conversionType` |
| `StepRoutine` | `classNameM`, `methodNameM`, `hanaRuntime` |
| `StepRead` | `objectName`, `objectType`, `errorHandling`, `timeDependencyType` |
| `StepTime` | (time-related) |
| `StepHierarchySplit` | (hierarchy splitting) |
| `StepInitial` | (initial value) |
| `StepNoUpdate` | (skip update) |
| `StepExpert` | (expert routine) |
| `StepODQMode` | (ODQ mode) |

### 21.5 DTPA (Data Transfer Process)

**EMF Namespace:** `http://www.sap.com/bw/modeling/DataTransferProcess.ecore`
**File Extension:** `.dtpa`
**Root Element:** `<dataTransferProcess>` (within `<DocumentRoot>`)

**Root `<dataTransferProcess>` Attributes:**
| Attribute | Type | Description |
|-----------|------|-------------|
| `name` | String | DTP technical name |
| `description` | String | Description |
| `type` | Enum | `STANDARD` or `FLEXIBLE` |

**Child Elements:**
| Element | Type | Cardinality | Description |
|---------|------|-------------|-------------|
| `generalInformation` | GeneralInformation | 0..1 | Metadata (tlogoProperties, texts) |
| `requestSelection` | RequestSelection | 0..1 | Delta/full load settings |
| `extractionSettings` | ExtractionSettings | 0..1 | Package size, parallel extraction |
| `execution` | Execution | 0..1 | Processing mode, SAP HANA execution |
| `runtimeProperties` | RuntimeProperties | 0..1 | Time interval, batch, temp storage |
| `source` | Source | 0..1 | Source definition |
| `target` | Target | 0..1 | Target definition |
| `filter` | Filter | 0..1 | Filter fields and routines |
| `errorHandling` | ErrorHandling | 0..1 | Error DTP, record tracking |
| `semanticGroup` | SemanticGroup | 0..1 | Semantic grouping |
| `programFlow` | ProgramFlow | 0..1 | Program flow nodes |
| `dtpExecution` | DtpExecution | 0..1 | Background/sync/simulation flags |

**Source Type Variants (one-of):**
- `<dataSource>` -- DataSource (RSDS)
- `<compositeProvider>` -- CompositeProvider (HCPR)
- `<dataStoreObject>` -- DataStore Object (ADSO)
- `<hanaAnalysisProcess>` -- HANA Analysis Process
- `<dataHubDataSet>` -- Data Hub DataSet

**Target Type Variants (one-of):**
- `<infoObjectMasterData>` -- InfoObject Master Data
- `<infoObjectHierarchies>` -- InfoObject Hierarchies
- `<dataStoreObject>` -- DataStore Object
- `<openHubDestination>` -- Open Hub Destination
- `<dataHubDataSet>` -- Data Hub DataSet

**Filter Element Children:**
- `<fields>` (0..*) -- FilterField with `name`, `field`, `selected`, `filterSelection`, `selectionType`
- `<selection>` -- within fields: `low`, `high`, `operator`, `excluding`

### 21.6 RSDS (DataSource)

**EMF Namespace:** `http://www.sap.com/bw/modeling/BwRsds.ecore`
**File Extension:** `.rsds`
**Root Element:** `<dataSource>` (within `<DocumentRoot>`)
**Content Handler:** EMF ResourceSet deserialization (`com.sap.bw.model.rsds`)

**Root `<dataSource>` is a `BwEntity` with:**
| Attribute | Type | Description |
|-----------|------|-------------|
| `name` | String | DataSource technical name |
| `description` | String | Description |
| `version` | String | Model version |

**Child Elements:**
| Element | Type | Cardinality | Description |
|---------|------|-------------|-------------|
| `tlogoProperties` | BwEntityProperties | 1..1 | Entity metadata |
| `segment` | Segment | 0..* | Data segments |

**Segment Element:**
| Child/Attribute | Type | Description |
|----------------|------|-------------|
| `ID` | String | Segment identifier |
| `description` | Reference | Segment descriptions |
| `technicalSegmentInfo` | Reference | Technical metadata |
| `fields` | DataSourceElement | 0..* | Field definitions |
| `keyField` | Reference | 0..* | Key field references |
| `languageField` | Reference | Language field |
| `dateFromField` | Reference | Date-from field |
| `dateToField` | Reference | Date-to field |

**DataSourceElement (extends BwElement):**
| Attribute | Type | Description |
|-----------|------|-------------|
| `key` | Boolean | Is key field |
| `fieldProperties` | FieldProperties | Field type/length/conversion |
| `description` | Reference | Field descriptions |

### 21.7 Additional Object Types

**DMOD (DataFlow Model):**
- EMF Namespace: `http://www.sap.com/bw/modeling/Dmod.ecore`
- Root: `<dataflow>` -- nodes with source/target connections, grid positions
- Content Handler: `DataFlowContentHandler`
- Content-Type: `application/vnd.sap.bw.modeling.dmod-v1_0_0+xml`
- Key elements: `<nodes>` with `objectName`, `objectType`, `gridPosX`, `gridPosY`, `exists`, `objectStatus`

**DEST (Open Hub Destination):**
- EMF Namespace: `http://www.sap.com/bw/modeling/dest.ecore`
- Root: `<destination>` -- 54 attributes covering file/DB/third-party targets
- Key elements: `destinationType` (enum), `fileProperties`, `databaseProperties`, `elements` (field list)

**FBP (Open ODS View):**
- EMF Namespace: `http://www.sap.com/bw/modeling/FBPModel.ecore`
- Root: `<openODSView>` -- semantics-based view with source object references
- Key elements: `semantics`, `requiredEntities`, `sourceObject` (with `objectName`, `objectType`, `sourceSystem`)

**TRCS (InfoSource):**
- EMF Namespace: `http://www.sap.com/bw/modeling/trcs.ecore`
- Root: `<infoSource>` -- 42 attributes, extends standard entity structure
- Key attributes: `aggregation`, `odsoLike`, `tlogoOwnedBy`, `objnmOwnedBy`

**SEGR (Semantic Group):**
- EMF Namespace: `http://www.sap.com/bw/modeling/Segr.ecore`
- Root: `<semanticGroup>` -- contains members with criteria
- Key elements: `<members>` with `name`, `status`, `criteria` (range-based filters)

---

## 22. Transport CTO Request/Response Bodies

This section documents the XML body structure for CTS (Change and Transport System) operations via the BW CTO (Cross-Transport Organizer) protocol.

### 22.1 Content Handlers

Three content handlers manage CTO XML:

| Handler | Root Element | Namespace | Content-Type | Direction |
|---------|-------------|-----------|-------------|-----------|
| `BwTransportOrganizer` | `bwCTO:transport` | `http://www.sap.com/bw/cto` | `application/vnd.sap.bw.modeling.cto-v1_1_0+xml` | Bidirectional |
| `TransportCollContentHandler` | `trCollect:objects` | `http://www.sap.com/bw/trcollect` | `application/vnd.sap-bw-modeling.trcollect+xml` | Deserialize only |
| `BwSystemContentHandler` | `bwCTO:system` | `http://www.sap.com/bw/cto` | `application/vnd.sap.bw.modeling.packages+xml` | Deserialize only |

### 22.2 Transport Check/Write Request Body

Both check and write operations POST the same request body structure:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<bwCTO:transport xmlns:bwCTO="http://www.sap.com/bw/cto">
  <objects>
    <object
      name="OBJECT_NAME"
      altName="ALTERNATIVE_NAME"
      isTransportName="true"
      type="TLOGO_TYPE"
      pgmid="LIMU"
      operation="I"
      uri="/sap/bw/modeling/..."
      corrnum="K900001"
      package="ZPACKAGE"
      author="USERNAME"
      masterlang="E"
      genflag=""
    />
    <!-- additional objects -->
  </objects>
</bwCTO:transport>
```

**Object Attributes:**
| Attribute | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `name` | String | Yes | -- | Object name in transport |
| `altName` | String | No | -- | Alternative/technical name |
| `isTransportName` | Boolean | Yes | -- | Whether `name` is the CTO delivery name |
| `type` | String | Yes | -- | Object type (TLOGO) |
| `pgmid` | String | Yes | -- | Program ID (usually `LIMU`) |
| `operation` | String | No | `I` | Operation code (`I`=Insert, `U`=Update, `D`=Delete) |
| `uri` | String | No | -- | Object ADT URI |
| `corrnum` | String | No | -- | Correction number (transport request) |
| `package` | String | No | `$TMP` | Package name |
| `author` | String | No | -- | Object author |
| `masterlang` | String | No | -- | Master language |
| `genflag` | String | No | -- | Generation flag |

### 22.3 Transport Check/Write Response Body

```xml
<?xml version="1.0" encoding="UTF-8"?>
<bwCTO:transport xmlns:bwCTO="http://www.sap.com/bw/cto">

  <properties>
    <property name="transportsChangeable" value="true" txt="..."/>
    <property name="bwChangeable" value="true" txt="..."/>
    <property name="basisChangeable" value="true" txt="..."/>
  </properties>

  <requests>
    <request corrnum="K900001" txt="Description" client="001"
             user="DEVELOPER" time="143022" date="20240115"
             function="K" status="D" target="QAS">
      <task corrnum="K900002" txt="Task" client="001"
            user="DEVELOPER" time="143022" date="20240115"
            function="S" status="D">
        <object objectName="NAME" objectType="TYPE"
                objectVersion="A" author="USER" package="PKG"
                srcSystem="SYS" masterlang="E" genflag=""
                createdOn="20240115" pos="1" text="Description"/>
      </task>
    </request>
  </requests>

  <bwrequests standard="K900001">
    <request corrnum="K900001" package="ZPACKAGE"/>
  </bwrequests>

  <changeability>
    <setting name="NAME" transportable="true"
             impVersion="1" changeable="true" tlogo="ADSO"/>
  </changeability>

  <objects>
    <object name="NAME" type="TYPE" pgmid="LIMU" ...>
      <lock>
        <request corrnum="K900001" ...>
          <task corrnum="K900002" .../>
        </request>
      </lock>
      <tadir isSupported="true" exists="true" existsGlobal="false"/>
    </object>
  </objects>

  <result recording="false" status="S" writeResult=""/>

</bwCTO:transport>
```

**Response Headers (in HTTP response, not XML):**
- `Writing-Enabled`: Boolean -- whether write operations are permitted
- `versionLevel`: Integer -- minimum 2 required for writing

**Result Status Values:**
| Value | Description |
|-------|-------------|
| `S` | Success |
| `E` | Error |
| `W` | Warning |

**TADir Status Attributes:**
| Attribute | Type | Description |
|-----------|------|-------------|
| `isSupported` | Boolean | TADir check supported |
| `exists` | Boolean | Object exists in TADir |
| `existsGlobal` | Boolean | Object exists globally |

### 22.4 Transport Collection Response Body

```xml
<?xml version="1.0" encoding="UTF-8"?>
<trCollect:objects xmlns:trCollect="http://www.sap.com/bw/trcollect">
  <details>
    <object objectName="NAME" objectType="TYPE"
            txt="Description" objectStatus="A"
            href="/sap/bw/modeling/..." hrefType="media-type"
            lastChangedBy="USER" lastChangedAt="20240115"/>
  </details>
  <group objectVersion="A">
    <object objectName="NAME" objectType="TYPE"
            objectVersion="A" author="USER" package="PKG"
            srcSystem="SYS" masterlang="E" genflag=""
            createdOn="20240115" associationType="103">
      <associatedObject objectName="RELATED" objectType="TYPE" .../>
    </object>
  </group>
</trCollect:objects>
```

### 22.5 System Information Response Body

```xml
<?xml version="1.0" encoding="UTF-8"?>
<bwCTO:system xmlns:bwCTO="http://www.sap.com/bw/cto"
              isCloudSystem="false"
              bwChangeable="true"
              basisChangeable="true">
  <package name="ZPACKAGE" description="My Package"
           originalLanguage="E" originalSystem="DEV">
    <component componentName="BW4HANA" componentVersion="200"
               vendor="sap.com"/>
  </package>
</bwCTO:system>
```

---

## 23. Additional Protocol Areas

The BW connectivity plugin implements several additional protocol areas beyond the core modeling operations. These are documented here for completeness.

### 23.1 Runtime/Reporting (BICS Query Execution)

**Content-Types:**
- Request: `application/vnd.sap.bw.modeling.bicsrequest-v1_1_0+xml`
- Response: `application/vnd.sap.bw.modeling.bicsresponse-v1_1_0+xml`

**Query Parameter:** `compid={QueryName}`, `dbgmode=true|false`

**Custom Request Headers:**
| Header | Values | Description |
|--------|--------|-------------|
| `MetadataOnly` | `true`/`false` | Fetch structure only |
| `InclMetadata` | `true`/`false` | Include schema in response |
| `InclObjectValues` | `true`/`false` | Include value helps |
| `InclExceptDef` | `true`/`false` | Include exception definitions |
| `CompactMode` | `true`/`false` | Compressed response |
| `FromRow` / `ToRow` | Integer | Result set row range |

**Operations:** GET (metadata), POST (execute with selections). Response contains `<metaData>`, `<selection>`, `<resultSet>` (axis headers + cells), `<exceptions>`.

### 23.2 Hierarchy Operations

**XML Element:** `<hierarchy>` with attributes `name`, `version`, `dateTo`
**Child Elements:** `<node>` (recursive) with `iobj`, `tid`, `txt`, `int`, `ext`, `leaf` attributes
**Methods:** GET (fetch), POST (create), PUT (update), DELETE (remove)

### 23.3 Data Volumes

**Namespace:** `http://www.sap.com/bw/dataVolumes` (prefix: `davo`)
**Root:** `<davo:dataVolumes isComplete="true|false">`
**Query Params:** `infoprovider={name}`, `maxrows={N}`
**Content:** Categories containing `<davo:adso>` elements with `tableName`, `hotWarmSize`, `hotWarmCount`, `coldSize`, `coldCount`, `loadUnit`, `partNo`

### 23.4 Virtual Folders

**Namespace:** `http://www.sap.com/bw/virtualFolders` (prefix: `bwVfs`)
**Root:** `<bwVfs:virtualFoldersResult>` or `<bwVfs:virtualPathResult>`
**Query Params:** `package={name}`, `objecttype={type}`, `user={name}`, `feedsize={N}` (default 600), `incloc=true|false`
**Content:** Expandable folder tree with `<bwVfs:pathFragment name="..." type="DEVC|FOLDER"/>` elements

### 23.5 Transfer Objects (Component Refactoring)

**Discovery:** `http://www.sap.com/bw/modeling/comprefactor` (term: `comprefactor`)
**Content-Types (versioned per component):**
- Query: `application/vnd.sap.bw.modeling.query-v1_11_0+xml`
- Variable: `application/vnd.sap.bw.modeling.variable-v1_10_0+xml`
- Structure: `application/vnd.sap.bw.modeling.structure-v1_9_0+xml`
- Filter: `application/vnd.sap.bw.modeling.filter-v1_9_0+xml`
- RKF: `application/vnd.sap.bw.modeling.rkf-v1_10_0+xml`
- CKF: `application/vnd.sap.bw.modeling.ckf-v1_10_0+xml`

**Query Params:** `mode={mode}`, `iprov={infoProvider}`, `subtype={type}`, `incldep=true|false`, `maxrows={N}`

### 23.6 Characteristic Values (Value Help)

**Namespace:** `http://www.sap.com/bw/chavalues` (prefix: `bwChaValues`)
**Root:** `<bwChaValues:infoObject name="..." hasTxt="true|false" isCompounded="true|false">`
**Value Response:** `<bwChaValues:valueHelp>` containing `<row sid="..." ext="..." int="..."/>` elements
**Query Params:** `characteristicname={name}`, `infoprovider={prov}`, `readmode=MASTER|TRANSACTIONAL`, `mostrecent=true|false`, `readsids=true|false`, `readtexts=true|false`, `maxrows={N}`

### 23.7 DTP Simulation

**Operations:** Query/monitor/cancel DTP simulation jobs
**XML Elements:** `<job guid="..." userName="..." timestamp="...">`, `<node txt="..." key="...">`, `<action txt="..." key="...">`
**Methods:** GET (query jobs), POST (execute), DELETE (cancel)

### 23.8 Data Subscription

**Discovery:** `http://www.sap.com/bw/modeling/dsub` (term: `dsub`)
**Namespace:** `http://www.sap.com/bw/datasubscription.ecore`
**Root:** `<dataSubscription>` with `name`, `description`, `entityProperties`, `version`
**Operations:** Search scenarios, read definitions, mass operations for data product generation

### 23.9 ODP (Operational Data Provisioning) Sources

**Content Handler:** `SourceODPContentHandler`
**Key Attributes:** `suppFull`, `suppDelta`, `suppRealtime` (extraction mode support flags), `semantics`
**Content:** ODP source definitions with segments and field metadata

---

## Appendix A: Quick Reference -- Common Operations

| Operation | Method | URL Pattern | Key Headers/Params |
|-----------|--------|-------------|-------------------|
| Search objects | GET | `/sap/bw/modeling/is/bwsearch?searchTerm=*&objectType=ADSO` | Accept: application/atom+xml |
| Read object | GET | `/sap/bw/modeling/{tlogo}/{name}/{version}` | Accept: versioned media type |
| Lock object | POST | `/sap/bw/modeling/{tlogo}/{name}?action=lock` | Stateful session, CSRF |
| Save object | PUT | `/sap/bw/modeling/{tlogo}/{name}?lockHandle={h}&corrNr={t}` | Stateful session, CSRF |
| Unlock object | POST | `/sap/bw/modeling/{tlogo}/{name}?action=unlock` | Stateful session, CSRF |
| Validate activation | POST | `{activation}?mode=validate` | `application/vnd.sap-bw-modeling.massact+xml` |
| Activate | POST | `{activation}?mode=activate` | `application/vnd.sap-bw-modeling.massact+xml` |
| Check transport | GET | `{cto}` with check relation | `application/vnd.sap.bw.modeling.cto-v1_1_0+xml` |
| Write to transport | POST | `{cto}?corrnum={t}` with write relation | `application/vnd.sap.bw.modeling.cto-v1_1_0+xml` |
| Poll job status | GET | `/sap/bw/modeling/jobs/{guid}/status` | `application/vnd.sap-bw-modeling.jobs.job.status+xml` |

## Appendix B: Runtime Status Values

| Key | Description |
|-----|-------------|
| `UNKNOWN` | Unknown |
| `NOEXEC` | Not Executed |
| `SUCCESS` | Success |
| `ERROR` | Error |

## Appendix C: Client Role Types

| Key | Description |
|-----|-------------|
| `P` | Production |
| `T` | Test |
| `C` | Customizing |
| `D` | Demo |
| `E` | Training/Education |
| `S` | SAP Reference |

## Appendix D: Content Installation Modes

| Mode | Description | Batch | Cloud |
|------|-------------|:-----:|:-----:|
| 0 | Simulate | No | No |
| 1 | Install | No | No |
| 2 | Install (Batch) | Yes | Yes |
| 3 | Install (Transport) | No | Yes |

## Appendix E: Content Merge Processing Modes

| Key | Description |
|-----|-------------|
| (empty) | No Activation |
| `C` | Copy Version |
| `M` | Merge (Active dominant) |
| `A` | Merge (Delivered dominant) |
