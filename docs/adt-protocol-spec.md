# SAP ADT REST Protocol Specification

Version 1.0 -- February 2026

---

## 1. Introduction

### 1.1 Purpose

This document is the primary protocol reference for building REST clients that interact with SAP ABAP Development Tools (ADT) endpoints. It documents the HTTP methods, URL paths, query parameters, request/response bodies, XML namespaces, media types, and behavioral patterns required to programmatically perform ABAP development operations against a SAP NetWeaver or SAP BTP ABAP Environment system.

### 1.2 Sources

This specification was reverse-engineered from three independent sources:

1. **Eclipse ADT Plugin Decompilation** -- 4298 Java class files across 34 plugin directories (plugin version 3.56.0). Provides canonical header definitions, session management internals, object type codes, and async operation patterns.

2. **abap-adt-api TypeScript Library** (MIT Licensed) -- Commit `cded3d8bec7f576a943e7434e766332b9d1e328e` of [marcellourbani/abap-adt-api](https://github.com/marcellourbani/abap-adt-api). Provides tested request/response pairs for all major endpoint groups.

3. **Captured Live ADT Discovery Document** -- Parsed from a running SAP ABAP Cloud Developer Trial system. Provides the authoritative list of all registered endpoints, accepted media types, URI templates, and category schemes.

Additionally, XML fixtures captured from real Eclipse ADT traffic are included as examples throughout.

### 1.3 Legal Basis

This specification documents the ADT REST protocol for interoperability purposes. Protocol analysis and documentation of undocumented APIs is lawful under interoperability provisions (EU Directive 2009/24/EC, US DMCA 17 USC 1201(f)). No SAP proprietary source code was copied or reproduced. The abap-adt-api library is MIT licensed. The Eclipse plugin analysis involved decompilation solely for the purpose of interface documentation.

### 1.4 Conventions

- URL paths are relative to the system base URL (e.g., `https://host:port`)
- `{variable}` denotes a URI template variable (RFC 6570)
- `{?param1,param2}` denotes optional query parameters
- All XML examples use the namespaces defined in Section 3
- "CSRF required" means the request must include a valid `x-csrf-token` header

---

## 2. Protocol Fundamentals

### 2.1 Base URL

All ADT endpoints are rooted under:

```
/sap/bc/adt/
```

The full base URL is `https://{host}:{port}/sap/bc/adt/`. HTTPS is mandatory for production use.

### 2.2 Authentication

ADT supports HTTP Basic Authentication over HTTPS:

```
Authorization: Basic {base64(username:password)}
```

Bearer token authentication is also supported on some systems:

```
Authorization: bearer {token}
```

### 2.3 CSRF Token Lifecycle

Every mutating request (POST, PUT, DELETE) requires a valid CSRF token. The token lifecycle is:

1. **Fetch**: Send any GET request with header `x-csrf-token: fetch`
2. **Receive**: Server returns the token in the `x-csrf-token` response header
3. **Use**: Include the token in all subsequent mutating requests via `x-csrf-token: {token}`
4. **Expiry detection**: On HTTP 403 with response header `x-csrf-token: Required`, the token has expired
5. **Re-fetch**: Send another GET with `x-csrf-token: fetch`, then retry the failed request
6. **Session timeout**: On HTTP 400 with status text `Session timed out`, re-login from scratch

The recommended login endpoint for fetching the initial CSRF token is:

```
GET /sap/bc/adt/compatibility/graph
x-csrf-token: fetch
X-sap-adt-sessiontype: stateless
Accept: */*
Cache-Control: no-cache
```

### 2.4 Session Management

Every request includes the `X-sap-adt-sessiontype` header:

| Value | Meaning |
|-------|---------|
| `stateful` | Server maintains session state between requests. Required for lock/edit/save workflows. |
| `stateless` | Each request is independent. No session state maintained. Default for read-only operations. |
| `` (empty) | Keep current session type unchanged. |

When stateful, the additional header `x-sap-adt-softstate: true` is also sent.

**Cookie handling**: The server returns `SAP_SESSIONID_*` cookies in `set-cookie` response headers. These must be included in all subsequent requests to maintain the session.

**Session context**: For stateful sessions, the `sap-contextid` cookie identifies the server-side session.

**Keep-alive**: To prevent stateful session timeout, send a GET to `/sap/bc/adt/compatibility/graph` every 120 seconds.

**Drop session**: To release a stateful session without logging out, send a GET to `/sap/bc/adt/compatibility/graph` with `X-sap-adt-sessiontype: stateless`.

**Logout**:
```
GET /sap/public/bc/icf/logoff
```
Clears all cookies and invalidates the CSRF token.

### 2.5 Content Negotiation

ADT uses versioned vendor media types for content negotiation:

```
Accept: application/vnd.sap.adt.{domain}.{resource}[.v{N}]+{format}
```

Formats include: `+xml`, `+json`, `+asjson` (SAP-specific JSON), `+html`, `+zip`.

When multiple versions are acceptable, use quality factors:

```
Accept: application/vnd.sap.adt.packages.v2+xml, application/vnd.sap.adt.packages.v1+xml;q=0.9
```

Many endpoints also accept the generic `application/*` content type.

### 2.6 Async Operations

Long-running operations (pull, activation, ATC checks) use an async polling pattern:

1. Client sends POST request to initiate the operation
2. Server responds with **HTTP 202 Accepted** and a `Location` header containing the polling URL
3. Client polls the polling URL with GET requests
4. Minimum polling interval: **10 seconds**
5. Poll response contains operation status:
   - `running` -- operation still in progress
   - `completed` -- operation finished successfully
   - `failed` -- operation failed

**Polling response structure**:
```xml
<adtcore:operation xmlns:adtcore="http://www.sap.com/adt/core"
                   adtcore:id="{operationId}"
                   adtcore:status="running|completed|failed"
                   adtcore:startedAt="{ISO8601}"
                   adtcore:finishedAt="{ISO8601}">
  <adtcore:description>{description}</adtcore:description>
  <adtcore:progress adtcore:percentage="{0-100}" adtcore:text="{statusText}"/>
  <adtcore:result>
    <!-- Operation-specific result payload when completed -->
  </adtcore:result>
</adtcore:operation>
```

Some endpoints support long polling via `withLongPolling=true` query parameter, where the server holds the connection open until the operation completes or a timeout occurs.

### 2.7 Error Response Format

ADT errors are returned as XML in the `exc:exception` format:

```xml
<exc:exception xmlns:exc="http://www.sap.com/adt/exceptions">
  <exc:namespace id="/SAP/BC/ADT"/>
  <exc:type>{errorType}</exc:type>
  <exc:message lang="EN">{errorMessage}</exc:message>
  <exc:localizedMessage lang="EN">{localizedMessage}</exc:localizedMessage>
  <properties>
    <entry key="conflictText">{value}</entry>
    <entry key="T100KEY-ID">{value}</entry>
    <entry key="T100KEY-NO">{value}</entry>
  </properties>
</exc:exception>
```

Note: The exception namespace varies. The decompiled plugins reference `http://www.sap.com/abapxml/types/communicationframework` while captured traffic sometimes uses `http://www.sap.com/adt/exceptions`.

### 2.8 Common Query Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `sap-client` | string | SAP client number (e.g., `100`) |
| `sap-language` | string | SAP language key (e.g., `EN`) |

These can also be sent as HTTP headers.

### 2.9 Common Headers

| Header | Direction | Description |
|--------|-----------|-------------|
| `x-csrf-token` | Request/Response | CSRF token (see Section 2.3) |
| `X-sap-adt-sessiontype` | Request | Session type: `stateful`, `stateless`, or empty |
| `x-sap-adt-softstate` | Request | `true` when session is stateful |
| `X-sap-adt-profiling` | Request/Response | `server-time` (request); `server-time=<millis>` (response) |
| `sap-language` | Request | SAP language code |
| `sap-client` | Request | SAP client number |
| `sap-adt-request-id` | Request | Unique request ID for push channel correlation |
| `Location` | Response | Polling URI for async operations (202 responses) |

---

## 3. XML Namespace Reference

### 3.1 Core Namespaces

| Prefix | URI | Usage |
|--------|-----|-------|
| `adtcore` | `http://www.sap.com/adt/core` | Core attributes: type, name, description, uri, version |
| `asx` | `http://www.sap.com/abapxml` | ABAP XML serialization (lock results, transport data) |
| `atom` | `http://www.w3.org/2005/Atom` | Atom links, feeds, entries |
| `app` | `http://www.w3.org/2007/app` | Atom Publishing Protocol (discovery documents) |
| `adtcomp` | `http://www.sap.com/adt/compatibility` | Template links in discovery documents |
| `adtrest` | `http://www.sap.com/adt/communication/rest` | REST communication framework |
| `exc` | `http://www.sap.com/abapxml/types/communicationframework` | Exception/error XML responses |

### 3.2 Object-Type Namespaces

| Prefix | URI | Usage |
|--------|-----|-------|
| `class` | `http://www.sap.com/adt/oo/classes` | ABAP classes |
| `intf` | `http://www.sap.com/adt/oo/interfaces` | ABAP interfaces |
| `program` | `http://www.sap.com/adt/programs/programs` | Programs |
| `include` | `http://www.sap.com/adt/programs/includes` | Includes |
| `group` | `http://www.sap.com/adt/functions/groups` | Function groups |
| `fmodule` | `http://www.sap.com/adt/functions/fmodules` | Function modules |
| `finclude` | `http://www.sap.com/adt/functions/fincludes` | Function group includes |
| `pak` | `http://www.sap.com/adt/packages` | Packages |
| `ddl` | `http://www.sap.com/adt/ddic/ddlsources` | CDS data definitions (DDL) |
| `dcl` | `http://www.sap.com/adt/acm/dclsources` | CDS access control (DCL) |
| `ddlx` | `http://www.sap.com/adt/ddic/ddlxsources` | CDS metadata extensions |
| `ddla` | `http://www.sap.com/adt/ddic/ddlasources` | CDS annotation definitions |
| `srvd` | `http://www.sap.com/adt/ddic/srvdsources` | Service definitions |
| `srvb` | `http://www.sap.com/adt/ddic/ServiceBindings` | Service bindings |
| `blue` | `http://www.sap.com/wbobj/blue` | Tables (DDIC "blue" objects) |
| `auth` | `http://www.sap.com/iam/auth` | Authorization fields |
| `susob` | `http://www.sap.com/iam/suso` | Authorization objects |
| `mc` | `http://www.sap.com/adt/MessageClass` | Message classes |

### 3.3 Domain-Specific Namespaces

| Prefix | URI | Usage |
|--------|-----|-------|
| `abapsource` | `http://www.sap.com/adt/abapsource` | ABAP source, element info |
| `chkrun` | `http://www.sap.com/adt/checkrun` | Syntax check, check runs |
| `chkl` | `http://www.sap.com/abapxml/checklist` | Activation check messages |
| `ioc` | `http://www.sap.com/abapxml/inactiveCtsObjects` | Inactive CTS objects |
| `aunit` | `http://www.sap.com/adt/aunit` | ABAP Unit tests |
| `atc` | `http://www.sap.com/adt/atc` | ATC checks |
| `atcexmpt` | `http://www.sap.com/adt/atc/exemption` | ATC exemptions |
| `atcfinding` | `http://www.sap.com/adt/atc/finding` | ATC findings |
| `tm` | `http://www.sap.com/cts/adt/tm` | Transport management |
| `usagereferences` | `http://www.sap.com/adt/ris/usageReferences` | Where-used references |
| `quickfixes` | `http://www.sap.com/adt/quickfixes` | Quick fix proposals |
| `generic` | `http://www.sap.com/adt/refactoring/genericrefactoring` | Generic refactoring |
| `rename` | `http://www.sap.com/adt/refactoring/renamerefactoring` | Rename refactoring |
| `extractmethod` | `http://www.sap.com/adt/refactoring/extractmethodrefactoring` | Extract method refactoring |
| `configuration` | `http://www.sap.com/adt/configuration` | Configuration management |
| `nameditem` | `http://www.sap.com/adt/nameditem` | Named item lists (value helps) |
| `prettyprintersettings` | `http://www.sap.com/adt/prettyprintersettings` | Pretty printer settings |
| `trc` | `http://www.sap.com/adt/runtime/traces/abaptraces` | Trace parameters |

### 3.4 abapGit Namespaces

| Prefix | URI | Usage |
|--------|-----|-------|
| `abapgitrepo` | `http://www.sap.com/adt/abapgit/repositories` | abapGit repositories |
| `abapgitexternalrepo` | `http://www.sap.com/adt/abapgit/externalRepo` | External repo info |
| `abapgitstaging` | `http://www.sap.com/adt/abapgit/staging` | abapGit staging |
| `abapObjects` | `http://www.sap.com/adt/abapgit/abapObjects` | Deserialized ABAP objects |

### 3.5 Link Relations

| URI | Usage |
|-----|-------|
| `http://www.sap.com/adt/relations/source` | Source code link |
| `http://www.sap.com/adt/relations/transport` | Transport annotation |
| `http://www.sap.com/adt/relations/versions` | Object versions |
| `http://www.sap.com/adt/relations/objectstructure` | Object structure |
| `http://www.sap.com/adt/relations/navigation` | Navigation |
| `http://www.sap.com/adt/relations/elementinfo` | Element info |
| `http://www.sap.com/adt/relations/mainprograms` | Main programs for includes |
| `http://www.sap.com/adt/relations/documentation` | Documentation |

---

## 4. Content-Type Catalog

### 4.1 Generic Types

| Content-Type | Usage |
|-------------|-------|
| `application/*` | Generic application content (most requests) |
| `application/xml` | Standard XML |
| `text/plain` | ABAP source code (read/write) |
| `text/plain; charset=utf-8` | ABAP source code (write) |
| `application/atom+xml;type=feed` | Atom feeds (users, revisions, dumps) |
| `application/atomsvc+xml` | Atom service document (discovery) |

### 4.2 Core ADT Types

| Content-Type | Usage |
|-------------|-------|
| `application/vnd.sap.adt.core.http.session.v1+xml` through `v3+xml` | HTTP session |
| `application/vnd.sap.adt.core.http.systeminformation.v1+json` | System information |
| `application/vnd.sap.adt.bgrun.v1+xml` | Background run polling |
| `application/vnd.sap.adt.logs+xml` | Logs |
| `application/vnd.sap.adt.nameditems.v1+xml` | Named item lists |
| `application/vnd.sap.adt.elementinfo+xml` | Element info |
| `application/vnd.sap.adt.objectstructure+xml` | Object structure |

### 4.3 Package Types

| Content-Type | Usage |
|-------------|-------|
| `application/vnd.sap.adt.packages.v1+xml` | Packages (v1) |
| `application/vnd.sap.adt.packages.v2+xml` | Packages (v2) |

### 4.4 Object-Oriented Types

| Content-Type | Usage |
|-------------|-------|
| `application/vnd.sap.adt.oo.classes+xml` through `v4+xml` | Classes |
| `application/vnd.sap.adt.oo.interfaces+xml` through `v5+xml` | Interfaces |
| `application/vnd.sap.adt.oo.classincludes+xml` | Class includes |

### 4.5 Program/Function Types

| Content-Type | Usage |
|-------------|-------|
| `application/vnd.sap.adt.programs.programs+xml` through `v3+xml` | Programs |
| `application/vnd.sap.adt.programs.includes+xml` through `v2+xml` | Includes |
| `application/vnd.sap.adt.functions.groups+xml` through `v3+xml` | Function groups |
| `application/vnd.sap.adt.functions.fmodules+xml` through `v3+xml` | Function modules |

### 4.6 DDIC / CDS Types

| Content-Type | Usage |
|-------------|-------|
| `application/vnd.sap.adt.ddlSource+xml` | DDL source |
| `application/vnd.sap.adt.tables.v2+xml` | Database tables |
| `application/vnd.sap.adt.structures.v2+xml` | Structures |
| `application/vnd.sap.adt.dataelements.v2+xml` | Data elements |
| `application/vnd.sap.adt.domains.v2+xml` | Domains |

### 4.7 Transport Types

| Content-Type | Usage |
|-------------|-------|
| `application/vnd.sap.as+xml;charset=UTF-8;dataname=com.sap.adt.lock.result` | Lock result |
| `application/vnd.sap.as+xml;charset=UTF-8;dataname=com.sap.adt.transport.service.checkData` | Transport check |
| `application/vnd.sap.as+xml; charset=UTF-8; dataname=com.sap.adt.CreateCorrectionRequest` | Create transport |
| `application/vnd.sap.adt.transportorganizer.v1+xml` | Transport organizer |

### 4.8 Test/Check Types

| Content-Type | Usage |
|-------------|-------|
| `application/vnd.sap.adt.abapunit.testruns.config.v1+xml` through `v4+xml` | AUnit test config |
| `application/vnd.sap.adt.abapunit.testruns.result.v1+xml` | AUnit test result |
| `application/vnd.sap.adt.checkmessages+xml` | Check messages |
| `application/vnd.sap.adt.checkobjects+xml` | Check objects |
| `application/vnd.sap.adt.inactivectsobjects.v1+xml` | Inactive CTS objects |
| `application/atc.worklist.v1+xml` | ATC worklist |

### 4.9 abapGit Types

| Content-Type | Usage |
|-------------|-------|
| `application/abapgit.adt.repos.v2+xml` | abapGit repository list |
| `application/abapgit.adt.repo.v3+xml` | abapGit repository (create/pull) |
| `application/abapgit.adt.repo.info.ext.request.v2+xml` | External repo info request |
| `application/abapgit.adt.repo.info.ext.response.v2+xml` | External repo info response |
| `application/abapgit.adt.repo.stage.v1+xml` | abapGit staging |

---

## 5. Discovery & Compatibility

### 5.1 ADT Discovery (Service Catalog)

- **Method:** GET
- **URL:** `/sap/bc/adt/discovery`
- **Accept:** `application/atomsvc+xml`
- **Session:** Stateless
- **CSRF:** Not required

**Response:** Atom Publishing Protocol service document listing all available workspaces and collections.

```xml
<!-- Captured from test/testdata/discovery_response.xml -->
<app:service xmlns:app="http://www.w3.org/2007/app"
             xmlns:atom="http://www.w3.org/2005/Atom"
             xmlns:adtcomp="http://www.sap.com/adt/compatibility">
  <app:workspace>
    <atom:title>Discovery</atom:title>
    <app:collection href="/sap/bc/adt/discovery">
      <atom:title>Discovery</atom:title>
      <adtcomp:templateLinks>
        <adtcomp:templateLink rel="http://www.sap.com/adt/relations/discovery"
          template="/sap/bc/adt/discovery" title="Discovery"
          type="application/atomsvc+xml"/>
      </adtcomp:templateLinks>
    </app:collection>
  </app:workspace>
  <app:workspace>
    <atom:title>Object Repository</atom:title>
    <app:collection href="/sap/bc/adt/packages">
      <atom:title>Packages</atom:title>
      <adtcomp:templateLinks>
        <adtcomp:templateLink rel="http://www.sap.com/adt/relations/packages"
          template="/sap/bc/adt/packages/{packageName}" title="Package"
          type="application/vnd.sap.adt.packages.v1+xml"/>
      </adtcomp:templateLinks>
    </app:collection>
  </app:workspace>
  <!-- ... additional workspaces ... -->
</app:service>
```

### 5.2 Core Discovery

- **Method:** GET
- **URL:** `/sap/bc/adt/core/discovery`
- **Response:** Atom service document with core ADT services

### 5.3 Compatibility Graph

- **Method:** GET
- **URL:** `/sap/bc/adt/compatibility/graph`
- **Also used for:** Login (CSRF fetch), keep-alive, session drop
- **Response:**

```xml
<compatibility:graph>
  <nodes>
    <node nameSpace="..." name="..."/>
  </nodes>
  <edges>
    <edge>
      <sourceNode nameSpace="..." name="..."/>
      <targetNode nameSpace="..." name="..."/>
    </edge>
  </edges>
</compatibility:graph>
```

### 5.4 Object Types (Information System)

- **Method:** GET
- **URL:** `/sap/bc/adt/repository/informationsystem/objecttypes`
- **Query Parameters:**

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `maxItemCount` | number | No | Maximum results (e.g., `999`) |
| `name` | string | No | Filter pattern (e.g., `*`) |
| `data` | string | No | e.g., `usedByProvider` |

- **Response:**

```xml
<nameditem:namedItemList>
  <nameditem:namedItem>
    <nameditem:name>CLAS/OC</nameditem:name>
    <nameditem:description>Global Class</nameditem:description>
    <nameditem:data>type:CLAS;usedBy:CLAS/OC</nameditem:data>
  </nameditem:namedItem>
</nameditem:namedItemList>
```

---

## 6. Repository Search & Navigation

### 6.1 Quick Search

- **Method:** GET
- **URL:** `/sap/bc/adt/repository/informationsystem/search`
- **Accept:** `application/*`
- **Session:** Stateless

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `operation` | string | Yes | Always `quickSearch` |
| `query` | string | Yes | Search term |
| `maxResults` | number | No | Default `100` |
| `objectType` | string | No | e.g., `PROG`, `CLAS` |

**Response:**

```xml
<adtcore:objectReferences xmlns:adtcore="http://www.sap.com/adt/core">
  <adtcore:objectReference adtcore:uri="/sap/bc/adt/oo/classes/zcl_example"
    adtcore:type="CLAS/OC" adtcore:name="ZCL_EXAMPLE"
    adtcore:description="Example class" adtcore:packageName="ZTEST_PKG"/>
</adtcore:objectReferences>
```

### 6.2 Find Object Path (Node Path)

- **Method:** POST
- **URL:** `/sap/bc/adt/repository/nodepath`
- **CSRF:** Required

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `uri` | string | Yes | Object URI |

**Response:**

```xml
<projectexplorer:nodepath>
  <projectexplorer:objectLinkReferences>
    <objectLinkReference adtcore:name="..." adtcore:type="..."
      adtcore:uri="..." projectexplorer:category="..."/>
  </projectexplorer:objectLinkReferences>
</projectexplorer:nodepath>
```

### 6.3 Repository Node Structure (Tree)

- **Method:** POST
- **URL:** `/sap/bc/adt/repository/nodestructure`
- **CSRF:** Required

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `parent_type` | string | Yes | `DEVC/K`, `PROG/P`, `FUGR/F`, `PROG/PI` |
| `parent_name` | string | No | Parent object name |
| `parent_tech_name` | string | No | Technical parent name |
| `user_name` | string | No | User filter |
| `withShortDescriptions` | boolean | Yes | Always `true` |
| `rebuild_tree` | string | No | `X` to force rebuild |

**Request body** (optional, for expanding specific nodes):

```xml
<asx:abap version="1.0" xmlns:asx="http://www.sap.com/abapxml">
  <asx:values>
    <DATA>
      <TV_NODEKEY>000001</TV_NODEKEY>
      <TV_NODEKEY>000002</TV_NODEKEY>
    </DATA>
  </asx:values>
</asx:abap>
```

**Response:**

```xml
<asx:abap xmlns:asx="http://www.sap.com/abapxml">
  <asx:values>
    <DATA>
      <TREE_CONTENT>
        <SEU_ADT_REPOSITORY_OBJ_NODE>
          <OBJECT_TYPE>CLAS/OC</OBJECT_TYPE>
          <OBJECT_NAME>ZCL_EXAMPLE</OBJECT_NAME>
          <TECH_NAME>ZCL_EXAMPLE</TECH_NAME>
          <OBJECT_URI>/sap/bc/adt/oo/classes/zcl_example</OBJECT_URI>
          <EXPANDABLE>X</EXPANDABLE>
          <NODE_ID>000001</NODE_ID>
          <DESCRIPTION>Example class</DESCRIPTION>
        </SEU_ADT_REPOSITORY_OBJ_NODE>
      </TREE_CONTENT>
    </DATA>
  </asx:values>
</asx:abap>
```

### 6.4 ABAP Documentation Lookup

- **Method:** POST
- **URL:** `/sap/bc/adt/docu/abap/langu`
- **Content-Type:** `text/plain`
- **Accept:** `application/vnd.sap.adt.docu.v1+html,text/html`

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `uri` | string | Yes | `{objectUri}#start={line},{column}` |
| `language` | string | No | e.g., `EN` |
| `format` | string | No | e.g., `eclipse` |

**Request body:** Plain text ABAP source
**Response:** HTML documentation

### 6.5 Package Value Help

- **Method:** GET
- **URL:** `/sap/bc/adt/packages/valuehelps/{type}`

Where `{type}` is one of: `applicationcomponents`, `softwarecomponents`, `transportlayers`, `translationrelevances`, `abaplanguageversions`

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `name` | string | No | Filter (default `*`) |

**Response:**

```xml
<nameditem:namedItemList>
  <nameditem:namedItem>
    <nameditem:name>LOCAL</nameditem:name>
    <nameditem:description>Local Development</nameditem:description>
  </nameditem:namedItem>
</nameditem:namedItemList>
```

---

## 7. Object Metadata (Structure)

### 7.1 Get Object Structure

- **Method:** GET
- **URL:** `{objectUrl}` -- varies by type (e.g., `/sap/bc/adt/oo/classes/{name}`, `/sap/bc/adt/programs/programs/{name}`)
- **Session:** Stateless

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `version` | string | No | `active`, `inactive`, `workingArea` |

**Response (Class example):**

```xml
<class:abapClass xmlns:class="http://www.sap.com/adt/oo/classes"
  xmlns:adtcore="http://www.sap.com/adt/core"
  adtcore:name="ZCL_EXAMPLE" adtcore:type="CLAS/OC"
  adtcore:changedAt="2026-01-15T10:30:00Z" adtcore:changedBy="DEVELOPER"
  adtcore:createdAt="2026-01-01T08:00:00Z" adtcore:description="Example class"
  adtcore:language="EN" adtcore:responsible="DEVELOPER" adtcore:version="active"
  abapsource:sourceUri="source/main" class:final="false" class:abstract="false"
  class:visibility="public" class:category="">
  <atom:link href="source/main" rel="http://www.sap.com/adt/relations/source"
    type="text/plain" etag="202601151030001"/>
  <class:include adtcore:name="CLAS/OC" class:includeType="main"
    adtcore:type="CLAS/OC" abapsource:sourceUri="source/main">
    <atom:link href="source/main" rel="http://www.sap.com/adt/relations/source"
      type="text/plain"/>
  </class:include>
  <class:include adtcore:name="CLAS/OCI" class:includeType="definitions"
    adtcore:type="CLAS/OCI" abapsource:sourceUri="includes/definitions">
    <atom:link href="includes/definitions" rel="http://www.sap.com/adt/relations/source"
      type="text/plain"/>
  </class:include>
</class:abapClass>
```

**Key attributes:**
- `adtcore:name`, `adtcore:type`, `adtcore:version`, `adtcore:description`
- `adtcore:changedAt`, `adtcore:changedBy`, `adtcore:createdAt`
- `adtcore:responsible`, `adtcore:language`
- `abapsource:sourceUri` -- relative path to source code
- Links with `type="text/plain"` point to source code endpoints

### 7.2 Object Component Structure

- **Method:** GET
- **URL:** `{objectUrl}/objectstructure`

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `version` | string | Yes | `active` |
| `withShortDescriptions` | boolean | Yes | `true` |

---

## 8. Source Code Read/Write

### 8.1 Read Source Code

- **Method:** GET
- **URL:** `{objectSourceUrl}` -- e.g., `/sap/bc/adt/programs/programs/{name}/source/main`
- **Accept:** `text/plain`
- **Session:** Stateless

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `version` | string | No | `active`, `inactive`, `workingArea` |

**Response:** Plain text source code

### 8.2 Write Source Code

- **Method:** PUT
- **URL:** `{objectSourceUrl}`
- **Content-Type:** `text/plain; charset=utf-8` (ABAP) or `application/*` (XML sources, detected by `<?xml` prefix)
- **Session:** STATEFUL required
- **CSRF:** Required

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `lockHandle` | string | Yes | Lock handle from lock operation |
| `corrNr` | string | No | Transport number |

**Request body:** Source code as plain text

---

## 9. Locking

### 9.1 Lock Object

- **Method:** POST
- **URL:** `{objectUrl}` (e.g., `/sap/bc/adt/oo/classes/zcl_example`)
- **Accept:** `application/*,application/vnd.sap.as+xml;charset=UTF-8;dataname=com.sap.adt.lock.result`
- **Session:** STATEFUL required
- **CSRF:** Required

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `_action` | string | Yes | `LOCK` |
| `accessMode` | string | No | Default `MODIFY` |

**Response:**

```xml
<asx:abap xmlns:asx="http://www.sap.com/abapxml">
  <asx:values>
    <DATA>
      <LOCK_HANDLE>{lockHandleToken}</LOCK_HANDLE>
      <CORRNR>{transportNumber}</CORRNR>
      <CORRUSER>{transportOwner}</CORRUSER>
      <CORRTEXT>{transportDescription}</CORRTEXT>
      <IS_LOCAL>X</IS_LOCAL>
      <IS_LINK_UP></IS_LINK_UP>
      <MODIFICATION_SUPPORT></MODIFICATION_SUPPORT>
    </DATA>
  </asx:values>
</asx:abap>
```

**Lock handle usage:** The `LOCK_HANDLE` value must be passed as the `lockHandle` query parameter on all subsequent PUT, POST, and DELETE operations on the locked object.

### 9.2 Unlock Object

- **Method:** POST
- **URL:** `{objectUrl}`
- **Session:** STATEFUL
- **CSRF:** Required

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `_action` | string | Yes | `UNLOCK` |
| `lockHandle` | string | Yes | URL-encoded lock handle |

---

## 10. Object Creation

### 10.1 Validate New Object

- **Method:** POST
- **URL:** `/sap/bc/adt/{validationPath}` -- varies by type (see table)
- **CSRF:** Required

Query parameters vary by type but typically include: `objname`, `description`, `objtype`, `packagename`.

**Response:**

```xml
<asx:abap xmlns:asx="http://www.sap.com/abapxml">
  <asx:values>
    <DATA>
      <SEVERITY>OK</SEVERITY>
      <SHORT_TEXT>Name is valid</SHORT_TEXT>
    </DATA>
  </asx:values>
</asx:abap>
```

### 10.2 Create Object

- **Method:** POST
- **URL:** `/sap/bc/adt/{creationPath}` -- varies by type (see table)
- **Content-Type:** `application/*`
- **CSRF:** Required

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `corrNr` | string | No | Transport number |

### 10.3 Creatable Object Types

| Type ID | Label | Creation Path | Validation Path | Root Element |
|---------|-------|--------------|-----------------|-------------|
| `PROG/P` | Program | `programs/programs` | `programs/validation` | `program:abapProgram` |
| `CLAS/OC` | Class | `oo/classes` | `oo/validation/objectname` | `class:abapClass` |
| `INTF/OI` | Interface | `oo/interfaces` | `oo/validation/objectname` | `intf:abapInterface` |
| `PROG/I` | Include | `programs/includes` | `includes/validation` | `include:abapInclude` |
| `FUGR/F` | Function Group | `functions/groups` | `functions/validation` | `group:abapFunctionGroup` |
| `FUGR/FF` | Function Module | `functions/groups/{grp}/fmodules` | `functions/validation` | `fmodule:abapFunctionModule` |
| `FUGR/I` | FG Include | `functions/groups/{grp}/includes` | `functions/validation` | `finclude:abapFunctionGroupInclude` |
| `DEVC/K` | Package | `packages` | `packages/validation` | `pak:package` |
| `DDLS/DF` | CDS Data Def | `ddic/ddl/sources` | `ddic/ddl/validation` | `ddl:ddlSource` |
| `DCLS/DL` | CDS Access Ctrl | `acm/dcl/sources` | `acm/dcl/validation` | `dcl:dclSource` |
| `DDLX/EX` | CDS Metadata Ext | `ddic/ddlx/sources` | `ddic/ddlx/sources/validation` | `ddlx:ddlxSource` |
| `DDLA/ADF` | CDS Annotation Def | `ddic/ddla/sources` | `ddic/ddla/sources/validation` | `ddla:ddlaSource` |
| `TABL/DT` | Table | `ddic/tables` | `ddic/tables/validation` | `blue:blueSource` |
| `DTEL/DE` | Data Element | `ddic/dataelements` | `ddic/dataelements/validation` | `blue:wbobj` |
| `SRVD/SRV` | Service Def | `ddic/srvd/sources` | `ddic/srvd/sources/validation` | `srvd:srvdSource` |
| `SRVB/SVB` | Service Binding | `businessservices/bindings` | `businessservices/bindings/validation` | `srvb:serviceBinding` |
| `MSAG/N` | Message Class | `messageclass` | `messageclass/validation` | `mc:messageClass` |
| `AUTH` | Auth Field | `aps/iam/auth` | `aps/iam/auth/validation` | `auth:auth` |
| `SUSO/B` | Auth Object | `aps/iam/suso` | `aps/iam/suso/validation` | `susob:suso` |

### 10.4 Create Simple Object (Example)

```xml
<?xml version="1.0" encoding="UTF-8"?>
<class:abapClass xmlns:class="http://www.sap.com/adt/oo/classes"
  xmlns:adtcore="http://www.sap.com/adt/core"
  adtcore:description="My new class"
  adtcore:name="ZCL_MY_CLASS" adtcore:type="CLAS/OC"
  adtcore:responsible="DEVELOPER">
  <adtcore:packageRef adtcore:name="ZTEST_PKG"/>
</class:abapClass>
```

### 10.5 Create Package (Example)

From captured test data (`test/testdata/package_create_request.xml`):

```xml
<?xml version="1.0" encoding="UTF-8"?>
<pak:package xmlns:pak="http://www.sap.com/adt/packages"
             xmlns:adtcore="http://www.sap.com/adt/core"
             adtcore:description="Test Package for erpl-deploy"
             adtcore:name="ZTEST_PKG"
             adtcore:type="DEVC/K"
             adtcore:version="active"
             adtcore:responsible="DEVELOPER">
  <adtcore:packageRef adtcore:name="$TMP"/>
  <pak:attributes pak:packageType="development"/>
  <pak:superPackage adtcore:name="$TMP"/>
  <pak:applicationComponent/>
  <pak:transport>
    <pak:softwareComponent pak:name="LOCAL"/>
    <pak:transportLayer pak:name=""/>
  </pak:transport>
  <pak:translation/>
  <pak:useAccesses/>
  <pak:packageInterfaces/>
  <pak:subPackages/>
</pak:package>
```

**Response** (201 Created, from `test/testdata/package_create_response.xml`):

```xml
<pak:package xmlns:pak="http://www.sap.com/adt/packages"
             xmlns:adtcore="http://www.sap.com/adt/core"
             adtcore:name="ZTEST_PKG" adtcore:type="DEVC/K"
             adtcore:uri="/sap/bc/adt/packages/ztest_pkg"
             adtcore:description="Test Package for erpl-deploy"
             adtcore:language="EN" adtcore:responsible="DEVELOPER"
             adtcore:masterLanguage="EN" adtcore:masterSystem="ABC"
             adtcore:version="active"
             adtcore:createdAt="2026-02-07T10:30:00Z"
             adtcore:changedAt="2026-02-07T10:30:00Z">
  <!-- ... same structure as request with server-populated fields ... -->
</pak:package>
```

---

## 11. Object Deletion

### 11.1 Delete Object

- **Method:** DELETE
- **URL:** `{objectUrl}`
- **Session:** STATEFUL required
- **CSRF:** Required

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `lockHandle` | string | Yes | Lock handle |
| `corrNr` | string | No | Transport number |

**Response:** Empty body on success; error XML on failure.

### 11.2 Object Registration Info (SSCR)

- **Method:** GET
- **URL:** `/sap/bc/adt/sscr/registration/objects`

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `uri` | string | Yes | Object URL |

---

## 12. Activation

### 12.1 Activate Objects (Mass Activation)

- **Method:** POST
- **URL:** `/sap/bc/adt/activation`
- **Content-Type:** `application/xml`
- **CSRF:** Required

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `method` | string | Yes | `activate` |
| `preauditRequested` | boolean | Yes | Typically `true` |

**Request body** (from `test/testdata/activation_request.xml`):

```xml
<adtcore:objectReferences xmlns:adtcore="http://www.sap.com/adt/core">
  <adtcore:objectReference adtcore:uri="/sap/bc/adt/oo/classes/zcl_test_flight"
                           adtcore:type="CLAS/OC"
                           adtcore:name="ZCL_TEST_FLIGHT"
                           adtcore:parentUri="/sap/bc/adt/packages/ztest_pkg"/>
  <adtcore:objectReference adtcore:uri="/sap/bc/adt/ddic/tables/ztest_flight_t"
                           adtcore:type="TABL/DT"
                           adtcore:name="ZTEST_FLIGHT_T"
                           adtcore:parentUri="/sap/bc/adt/packages/ztest_pkg"/>
</adtcore:objectReferences>
```

**Response** (from `test/testdata/activation_response.xml`):

```xml
<chkl:activationResponse xmlns:chkl="http://www.sap.com/abapxml/checklist"
                         xmlns:ioc="http://www.sap.com/abapxml/inactiveCtsObjects"
                         xmlns:adtcore="http://www.sap.com/adt/core">
  <chkl:messages>
    <msg objDescr="Class ZCL_TEST_FLIGHT" type="W" line="0"
         href="/sap/bc/adt/oo/classes/zcl_test_flight" forceSupported="false">
      <shortText><txt>Warning: Some method implementations are empty</txt></shortText>
    </msg>
  </chkl:messages>
  <ioc:inactiveObjects/>
</chkl:activationResponse>
```

**Success determination:** Activation succeeded if the response body is empty, or if there are no inactive objects and no error messages with type matching `[EAX]`.

Message types: `E` = Error, `W` = Warning, `I` = Information, `A` = Abort, `X` = Exception, `S` = Success.

### 12.2 Get Inactive Objects

- **Method:** GET
- **URL:** `/sap/bc/adt/activation/inactiveobjects`
- **Accept:** `application/vnd.sap.adt.inactivectsobjects.v1+xml, application/xml;q=0.8`

**Response** (from `test/testdata/inactive_objects_response.xml`):

```xml
<ioc:inactiveObjects xmlns:ioc="http://www.sap.com/abapxml/inactiveCtsObjects"
                     xmlns:adtcore="http://www.sap.com/adt/core">
  <ioc:entry>
    <ioc:object>
      <ioc:ref adtcore:uri="/sap/bc/adt/oo/classes/zcl_test_flight"
               adtcore:type="CLAS/OC"
               adtcore:name="ZCL_TEST_FLIGHT"
               adtcore:parentUri="/sap/bc/adt/packages/ztest_pkg"/>
    </ioc:object>
  </ioc:entry>
</ioc:inactiveObjects>
```

### 12.3 Get Main Programs (for includes)

- **Method:** GET
- **URL:** `{includeUrl}/mainprograms`
- **Response:** `adtcore:objectReferences` list

---

## 13. Syntax Check & Code Quality

### 13.1 Syntax Check Reporters

- **Method:** GET
- **URL:** `/sap/bc/adt/checkruns/reporters`

### 13.2 Syntax Check (ABAP)

- **Method:** POST
- **URL:** `/sap/bc/adt/checkruns?reporters=abapCheckRun`
- **Content-Type:** `application/*`
- **CSRF:** Required

**Request body:**

```xml
<?xml version="1.0" encoding="UTF-8"?>
<chkrun:checkObjectList xmlns:chkrun="http://www.sap.com/adt/checkrun"
  xmlns:adtcore="http://www.sap.com/adt/core">
  <chkrun:checkObject adtcore:uri="{sourceUrl}" chkrun:version="active">
    <chkrun:artifacts>
      <chkrun:artifact chkrun:contentType="text/plain; charset=utf-8"
        chkrun:uri="{includeUrl}">
        <chkrun:content>{base64_encoded_source}</chkrun:content>
      </chkrun:artifact>
    </chkrun:artifacts>
  </chkrun:checkObject>
</chkrun:checkObjectList>
```

**Response:**

```xml
<chkrun:checkRunReports>
  <chkrun:checkReport>
    <chkrun:checkMessageList>
      <chkrun:checkMessage chkrun:uri="{uri}#start={line},{offset}"
        chkrun:type="E" chkrun:shortText="Variable X is not defined"/>
    </chkrun:checkMessageList>
  </chkrun:checkReport>
</chkrun:checkRunReports>
```

---

## 14. ABAP Unit Testing

### 14.1 Run Unit Tests

- **Method:** POST
- **URL:** `/sap/bc/adt/abapunit/testruns`
- **Content-Type:** `application/*`
- **Accept:** `application/*`
- **CSRF:** Required

**Request body:**

```xml
<?xml version="1.0" encoding="UTF-8"?>
<aunit:runConfiguration xmlns:aunit="http://www.sap.com/adt/aunit">
  <external>
    <coverage active="false"/>
  </external>
  <options>
    <uriType value="semantic"/>
    <testDeterminationStrategy sameProgram="true" assignedTests="false"/>
    <testRiskLevels harmless="true" dangerous="true" critical="true"/>
    <testDurations short="true" medium="true" long="true"/>
    <withNavigationUri enabled="true"/>
  </options>
  <adtcore:objectSets xmlns:adtcore="http://www.sap.com/adt/core">
    <objectSet kind="inclusive">
      <adtcore:objectReferences>
        <adtcore:objectReference adtcore:uri="{objectUrl}"/>
      </adtcore:objectReferences>
    </objectSet>
  </adtcore:objectSets>
</aunit:runConfiguration>
```

**Response:**

```xml
<aunit:runResult>
  <program>
    <testClasses>
      <testClass adtcore:uri="..." adtcore:type="..." adtcore:name="..."
        durationCategory="..." riskLevel="...">
        <testMethods>
          <testMethod adtcore:uri="..." adtcore:name="..."
            executionTime="123" unit="ms">
            <alerts>
              <alert kind="failedAssertion" severity="critical">
                <title>Assertion failed</title>
                <details><detail text="Expected 1 but got 2"/></details>
                <stack>
                  <stackEntry adtcore:uri="..." adtcore:description="line 42"/>
                </stack>
              </alert>
            </alerts>
          </testMethod>
        </testMethods>
      </testClass>
    </testClasses>
  </program>
</aunit:runResult>
```

---

## 15. ATC Checks

### 15.1 ATC Customizing

- **Method:** GET
- **URL:** `/sap/bc/adt/atc/customizing`
- **Accept:** `application/xml, application/vnd.sap.atc.customizing-v1+xml`

### 15.2 Create ATC Worklist

- **Method:** POST
- **URL:** `/sap/bc/adt/atc/worklists`
- **Accept:** `text/plain`
- **CSRF:** Required

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `checkVariant` | string | Yes | Check variant name |

**Response:** Plain text worklist ID

### 15.3 Create ATC Run

- **Method:** POST
- **URL:** `/sap/bc/adt/atc/runs`
- **Content-Type:** `application/xml`
- **Accept:** `application/xml`
- **CSRF:** Required

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `worklistId` | string | Yes | Worklist ID from 15.2 |

**Request body:**

```xml
<?xml version="1.0" encoding="UTF-8"?>
<atc:run maximumVerdicts="100" xmlns:atc="http://www.sap.com/adt/atc">
  <objectSets xmlns:adtcore="http://www.sap.com/adt/core">
    <objectSet kind="inclusive">
      <adtcore:objectReferences>
        <adtcore:objectReference adtcore:uri="/sap/bc/adt/packages/ztest_pkg"/>
      </adtcore:objectReferences>
    </objectSet>
  </objectSets>
</atc:run>
```

### 15.4 Get ATC Worklist Results

- **Method:** GET
- **URL:** `/sap/bc/adt/atc/worklists/{worklistId}`
- **Accept:** `application/atc.worklist.v1+xml`

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `timestamp` | number | No | Worklist timestamp |
| `includeExemptedFindings` | boolean | No | Default `false` |

---

## 16. Transport Management

### 16.1 Transport Check

- **Method:** POST
- **URL:** `/sap/bc/adt/cts/transportchecks`
- **Content-Type:** `application/vnd.sap.as+xml; charset=UTF-8; dataname=com.sap.adt.transport.service.checkData`
- **Accept:** `application/vnd.sap.as+xml;charset=UTF-8;dataname=com.sap.adt.transport.service.checkData`
- **CSRF:** Required

**Request body:**

```xml
<?xml version="1.0" encoding="UTF-8"?>
<asx:abap xmlns:asx="http://www.sap.com/abapxml" version="1.0">
  <asx:values>
    <DATA>
      <DEVCLASS>ZTEST_PKG</DEVCLASS>
      <OPERATION>I</OPERATION>
      <URI>/sap/bc/adt/oo/classes/zcl_example</URI>
    </DATA>
  </asx:values>
</asx:abap>
```

### 16.2 Create Transport

- **Method:** POST
- **URL:** `/sap/bc/adt/cts/transports`
- **Content-Type:** `application/vnd.sap.as+xml; charset=UTF-8; dataname=com.sap.adt.CreateCorrectionRequest`
- **Accept:** `text/plain`
- **CSRF:** Required

**Response:** Plain text -- transport number is last path segment (e.g., `NPLK900001`)

### 16.3 List User Transports

- **Method:** GET
- **URL:** `/sap/bc/adt/cts/transportrequests`
- **Accept:** `application/vnd.sap.adt.transportorganizer.v1+xml`

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `user` | string | Yes | Username |
| `targets` | boolean | No | Default `true` |

### 16.4 Release Transport

- **Method:** POST
- **URL:** `/sap/bc/adt/cts/transportrequests/{number}/{action}`
- **CSRF:** Required

Where `{action}` is: `newreleasejobs` | `relwithignlock` (ignore locks) | `relObjigchkatc` (ignore ATC)

### 16.5 Delete Transport

- **Method:** DELETE
- **URL:** `/sap/bc/adt/cts/transportrequests/{number}`

---

## 17. Package Management

### 17.1 Get Package

- **Method:** GET
- **URL:** `/sap/bc/adt/packages/{packageName}`
- **Accept:** `application/vnd.sap.adt.packages.v1+xml`
- **Session:** Stateless

**Response** (200 OK, from `test/testdata/package_get_200.xml`):

```xml
<pak:package xmlns:pak="http://www.sap.com/adt/packages"
             xmlns:adtcore="http://www.sap.com/adt/core"
             adtcore:name="ZTEST_PKG" adtcore:type="DEVC/K"
             adtcore:uri="/sap/bc/adt/packages/ztest_pkg"
             adtcore:description="Test Package for erpl-deploy"
             adtcore:language="EN" adtcore:responsible="DEVELOPER"
             adtcore:version="active">
  <adtcore:packageRef adtcore:uri="/sap/bc/adt/packages/%24tmp" adtcore:name="$TMP"/>
  <pak:attributes pak:packageType="development"/>
  <pak:superPackage adtcore:name="$TMP" adtcore:uri="/sap/bc/adt/packages/%24tmp"/>
  <pak:applicationComponent/>
  <pak:transport>
    <pak:softwareComponent pak:name="LOCAL"/>
    <pak:transportLayer pak:name=""/>
  </pak:transport>
  <pak:translation/>
  <pak:useAccesses/>
  <pak:packageInterfaces/>
  <pak:subPackages/>
</pak:package>
```

**Response** (404 Not Found, from `test/testdata/package_get_404.xml`):

```xml
<exc:exception xmlns:exc="http://www.sap.com/adt/exceptions">
  <exc:namespace id="/SAP/BC/ADT"/>
  <exc:type>not_found</exc:type>
  <exc:message lang="EN">Package ZNONEXISTENT does not exist</exc:message>
  <exc:localizedMessage lang="EN">Package ZNONEXISTENT does not exist</exc:localizedMessage>
</exc:exception>
```

### 17.2 Create Package

See Section 10.5 above for the full create package example.

### 17.3 Package Settings

- **Method:** GET
- **URL:** `/sap/bc/adt/packages/settings`

---

## 18. Code Intelligence

### 18.1 Code Completion (Proposals)

- **Method:** POST
- **URL:** `/sap/bc/adt/abapsource/codecompletion/proposal`
- **Content-Type:** `application/*`
- **CSRF:** Required

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `uri` | string | Yes | `{sourceUrl}#start={line},{offset}` |
| `signalCompleteness` | boolean | Yes | `true` |

**Request body:** ABAP source code as text

### 18.2 Code Completion (Insertion)

- **Method:** POST
- **URL:** `/sap/bc/adt/abapsource/codecompletion/insertion`

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `uri` | string | Yes | `{sourceUrl}#start={line},{offset}` |
| `patternKey` | string | Yes | Pattern identifier from proposal |

### 18.3 Code Element Info

- **Method:** POST
- **URL:** `/sap/bc/adt/abapsource/codecompletion/elementinfo`
- **Content-Type:** `text/plain`
- **Accept:** `application/*`

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `uri` | string | Yes | `{sourceUrl}#start={line},{offset}` |

**Request body:** ABAP source code

### 18.4 Find Definition / Implementation (Navigation)

- **Method:** POST
- **URL:** `/sap/bc/adt/navigation/target`
- **Content-Type:** `text/plain`
- **Accept:** `application/*`

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `uri` | string | Yes | `{url}#start={line},{firstCol};end={line},{lastCol}` |
| `filter` | string | Yes | `definition` or `implementation` |

**Request body:** ABAP source code
**Response:**

```xml
<adtcore:objectReference adtcore:uri="{targetUrl}#start={line},{column}"/>
```

### 18.5 Pretty Printer

**Read settings:**
- **Method:** GET
- **URL:** `/sap/bc/adt/abapsource/prettyprinter/settings`

**Format source:**
- **Method:** POST
- **URL:** `/sap/bc/adt/abapsource/prettyprinter`
- **Content-Type:** `text/plain`
- **Accept:** `text/plain`
- **Request body:** ABAP source code
- **Response:** Formatted source code

### 18.6 Type Hierarchy

- **Method:** POST
- **URL:** `/sap/bc/adt/abapsource/typehierarchy`
- **Content-Type:** `text/plain`

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `uri` | string | Yes | `{url}#start={line},{offset}` |
| `type` | string | Yes | `superTypes` or `subTypes` |

---

## 19. Where-Used / Usage References

### 19.1 Usage References

- **Method:** POST
- **URL:** `/sap/bc/adt/repository/informationsystem/usageReferences`
- **Content-Type:** `application/*`
- **Accept:** `application/*`
- **CSRF:** Required

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `uri` | string | Yes | `{objectUrl}` or `{objectUrl}#start={line},{column}` |

**Request body:**

```xml
<?xml version="1.0" encoding="ASCII"?>
<usagereferences:usageReferenceRequest
  xmlns:usagereferences="http://www.sap.com/adt/ris/usageReferences">
  <usagereferences:affectedObjects/>
</usagereferences:usageReferenceRequest>
```

### 19.2 Usage Snippets

- **Method:** POST
- **URL:** `/sap/bc/adt/repository/informationsystem/usageSnippets`

---

## 20. Refactoring

### 20.1 Quick Fix Proposals

- **Method:** POST
- **URL:** `/sap/bc/adt/quickfixes/evaluation`

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `uri` | string | Yes | `{uri}#start={line},{column}` |

### 20.2 Rename (Evaluate / Preview / Execute)

All three steps use the same endpoint with different `step` parameter:

- **Method:** POST
- **URL:** `/sap/bc/adt/refactorings`
- **CSRF:** Required

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `step` | string | Yes | `evaluate`, `preview`, or `execute` |
| `rel` | string | Yes | `http://www.sap.com/adt/relations/refactoring/rename` |
| `uri` | string | For evaluate | `{uri}#start={line},{startCol};end={line},{endCol}` |

### 20.3 Extract Method

Same endpoint pattern with `rel=http://www.sap.com/adt/relations/refactoring/extractmethod`.

### 20.4 Change Package

Same endpoint pattern with `rel=http://www.sap.com/adt/relations/refactoring/changepackage`.

---

## 21. CDS / Data Dictionary

### 21.1 CDS Annotation Definitions

- **Method:** GET
- **URL:** `/sap/bc/adt/ddic/cds/annotation/definitions`
- **Accept:** `application/vnd.sap.adt.cds.annotation.definitions.v1+xml, application/vnd.sap.adt.cds.annotation.definitions.v2+xml`

### 21.2 DDIC Element Info

- **Method:** GET
- **URL:** `/sap/bc/adt/ddic/ddl/elementinfo`

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `getTargetForAssociation` | boolean | No | |
| `getExtensionViews` | boolean | No | |
| `getSecondaryObjects` | boolean | No | |
| `path` | string | No | Can be repeated for multiple paths |

### 21.3 DDL Repository Access

- **Method:** GET
- **URL:** `/sap/bc/adt/ddic/ddl/ddicrepositoryaccess`

### 21.4 Publish Service Binding

- **Method:** POST
- **URL:** `/sap/bc/adt/businessservices/odatav2/publishjobs`

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `servicename` | string | Yes | Service name |
| `serviceversion` | string | Yes | Service version |

**Request body:**

```xml
<adtcore:objectReferences xmlns:adtcore="http://www.sap.com/adt/core">
  <adtcore:objectReference adtcore:name="{bindingName}"/>
</adtcore:objectReferences>
```

---

## 22. abapGit Integration

### 22.1 List Git Repositories

- **Method:** GET
- **URL:** `/sap/bc/adt/abapgit/repos`
- **Accept:** `application/abapgit.adt.repos.v2+xml`
- **Session:** Stateless

**Response** (from `test/testdata/repo_list_response.xml`):

```xml
<abapgitrepo:repositories xmlns:abapgitrepo="http://www.sap.com/adt/abapgit/repositories"
                          xmlns:atom="http://www.w3.org/2005/Atom">
  <abapgitrepo:repository>
    <abapgitrepo:key>0242AC1100021EDEB4B4BD0C4F2B8C30</abapgitrepo:key>
    <abapgitrepo:package>ZTEST_PKG</abapgitrepo:package>
    <abapgitrepo:url>https://github.com/SAP-samples/abap-platform-refscen-flight.git</abapgitrepo:url>
    <abapgitrepo:branchName>refs/heads/main</abapgitrepo:branchName>
    <abapgitrepo:createdBy>DEVELOPER</abapgitrepo:createdBy>
    <abapgitrepo:createdAt>20260207103000</abapgitrepo:createdAt>
    <abapgitrepo:deserializedBy>DEVELOPER</abapgitrepo:deserializedBy>
    <abapgitrepo:deserializedAt>20260207103500</abapgitrepo:deserializedAt>
    <abapgitrepo:status>A</abapgitrepo:status>
    <abapgitrepo:statusText>Active</abapgitrepo:statusText>
    <atom:link href="/sap/bc/adt/abapgit/repos/{key}/pull"
      rel="http://www.sap.com/adt/abapgit/relations/pull" type="pull_link"/>
    <atom:link href="/sap/bc/adt/abapgit/repos/{key}/stage"
      rel="http://www.sap.com/adt/abapgit/relations/stage" type="stage_link"/>
    <atom:link href="/sap/bc/adt/abapgit/repos/{key}/push"
      rel="http://www.sap.com/adt/abapgit/relations/push" type="push_link"/>
    <atom:link href="/sap/bc/adt/abapgit/repos/{key}/checks"
      rel="http://www.sap.com/adt/abapgit/relations/check" type="check_link"/>
    <atom:link href="/sap/bc/adt/abapgit/repos/{key}/status"
      rel="http://www.sap.com/adt/abapgit/relations/status" type="status_link"/>
    <atom:link href="/sap/bc/adt/abapgit/repos/{key}/log"
      rel="http://www.sap.com/adt/abapgit/relations/log" type="log_link"/>
  </abapgitrepo:repository>
</abapgitrepo:repositories>
```

### 22.2 External Repo Info (Branches)

- **Method:** POST
- **URL:** `/sap/bc/adt/abapgit/externalrepoinfo`
- **Content-Type:** `application/abapgit.adt.repo.info.ext.request.v2+xml`
- **Accept:** `application/abapgit.adt.repo.info.ext.response.v2+xml`
- **CSRF:** Required

**Request body:**

```xml
<?xml version="1.0" ?>
<abapgitexternalrepo:externalRepoInfoRequest
  xmlns:abapgitexternalrepo="http://www.sap.com/adt/abapgit/externalRepo">
  <abapgitexternalrepo:url>{repoUrl}</abapgitexternalrepo:url>
  <abapgitexternalrepo:user>{user}</abapgitexternalrepo:user>
  <abapgitexternalrepo:password>{password}</abapgitexternalrepo:password>
</abapgitexternalrepo:externalRepoInfoRequest>
```

**Response:**

```xml
<externalRepoInfo>
  <accessMode>PUBLIC</accessMode>
  <branch>
    <name>refs/heads/main</name>
    <type>head</type>
    <sha1>abc123...</sha1>
    <displayName>main</displayName>
    <is_head>X</is_head>
  </branch>
</externalRepoInfo>
```

### 22.3 Clone (Create) Repository

- **Method:** POST
- **URL:** `/sap/bc/adt/abapgit/repos`
- **Content-Type:** `application/abapgit.adt.repo.v3+xml`
- **CSRF:** Required

**Request body** (from `test/testdata/repo_clone_request.xml`):

```xml
<abapgitrepo:repository xmlns:abapgitrepo="http://www.sap.com/adt/abapgit/repositories">
  <abapgitrepo:package>ZTEST_PKG</abapgitrepo:package>
  <abapgitrepo:url>https://github.com/SAP-samples/abap-platform-refscen-flight.git</abapgitrepo:url>
  <abapgitrepo:branchName>refs/heads/main</abapgitrepo:branchName>
  <abapgitrepo:transportRequest></abapgitrepo:transportRequest>
  <abapgitrepo:remoteUser></abapgitrepo:remoteUser>
  <abapgitrepo:remotePassword></abapgitrepo:remotePassword>
</abapgitrepo:repository>
```

**Response** (200 OK, from `test/testdata/repo_clone_response.xml`):

```xml
<abapObjects:abapObjects xmlns:abapObjects="http://www.sap.com/adt/abapgit/abapObjects">
  <abapObjects:abapObject>
    <abapObjects:type>CLAS</abapObjects:type>
    <abapObjects:name>ZCL_TEST_FLIGHT</abapObjects:name>
    <abapObjects:package>ZTEST_PKG</abapObjects:package>
    <abapObjects:status>A</abapObjects:status>
    <abapObjects:msgType>S</abapObjects:msgType>
    <abapObjects:msgText>Object deserialized successfully</abapObjects:msgText>
  </abapObjects:abapObject>
</abapObjects:abapObjects>
```

### 22.4 Pull Repository

- **Method:** POST
- **URL:** `/sap/bc/adt/abapgit/repos/{repoKey}/pull`
- **Content-Type:** `application/abapgit.adt.repo.v3+xml`
- **CSRF:** Required
- **Async:** Returns 202 Accepted with `Location` header

**Request body:**

```xml
<abapgitrepo:repository xmlns:abapgitrepo="http://www.sap.com/adt/abapgit/repositories">
  <abapgitrepo:branchName>refs/heads/main</abapgitrepo:branchName>
  <abapgitrepo:transportRequest>{transport}</abapgitrepo:transportRequest>
  <abapgitrepo:remoteUser>{user}</abapgitrepo:remoteUser>
  <abapgitrepo:remotePassword>{password}</abapgitrepo:remotePassword>
</abapgitrepo:repository>
```

**Response** (202 Accepted):
- `Location: /sap/bc/adt/operations/{operationId}`
- Body from `test/testdata/pull_202_response.xml`:

```xml
<abapgitrepo:repository xmlns:abapgitrepo="http://www.sap.com/adt/abapgit/repositories">
  <abapgitrepo:branchName>refs/heads/main</abapgitrepo:branchName>
</abapgitrepo:repository>
```

**Polling** (from `test/testdata/poll_running.xml`):

```xml
<adtcore:operation xmlns:adtcore="http://www.sap.com/adt/core"
                   adtcore:id="0242AC1100021EDEB4B4BD0C4F2BAAFF"
                   adtcore:status="running"
                   adtcore:startedAt="2026-02-07T10:35:00Z">
  <adtcore:description>Pull repository ZTEST_PKG</adtcore:description>
  <adtcore:progress adtcore:percentage="45" adtcore:text="Deserializing objects..."/>
</adtcore:operation>
```

**Completed** (from `test/testdata/poll_completed.xml`):

```xml
<adtcore:operation xmlns:adtcore="http://www.sap.com/adt/core"
                   adtcore:id="0242AC1100021EDEB4B4BD0C4F2BAAFF"
                   adtcore:status="completed"
                   adtcore:startedAt="2026-02-07T10:35:00Z"
                   adtcore:finishedAt="2026-02-07T10:36:15Z">
  <adtcore:description>Pull repository ZTEST_PKG</adtcore:description>
  <adtcore:progress adtcore:percentage="100" adtcore:text="Completed"/>
  <adtcore:result>
    <abapObjects:abapObjects xmlns:abapObjects="http://www.sap.com/adt/abapgit/abapObjects">
      <abapObjects:abapObject>
        <abapObjects:type>CLAS</abapObjects:type>
        <abapObjects:name>ZCL_TEST_FLIGHT</abapObjects:name>
        <abapObjects:package>ZTEST_PKG</abapObjects:package>
        <abapObjects:status>A</abapObjects:status>
        <abapObjects:msgType>S</abapObjects:msgType>
        <abapObjects:msgText>Object deserialized successfully</abapObjects:msgText>
      </abapObjects:abapObject>
    </abapObjects:abapObjects>
  </adtcore:result>
</adtcore:operation>
```

### 22.5 Unlink (Delete) Repository

- **Method:** DELETE
- **URL:** `/sap/bc/adt/abapgit/repos/{repoKey}`
- **Content-Type:** `application/abapgit.adt.repo.v3+xml`
- **CSRF:** Required

### 22.6 Get Staging

- **Method:** GET
- **URL:** `{stage_link.href}` -- from repo links where `type="stage_link"`
- **Content-Type:** `application/abapgit.adt.repo.stage.v1+xml`
- **Optional headers:** `Username`, `Password` (base64-encoded)

### 22.7 Push (Commit)

- **Method:** POST
- **URL:** `{push_link.href}` -- from repo links where `type="push_link"`
- **Content-Type:** `application/abapgit.adt.repo.stage.v1+xml`
- **Accept:** `application/abapgit.adt.repo.stage.v1+xml`
- **CSRF:** Required

### 22.8 Switch Branch

- **Method:** POST
- **URL:** `/sap/bc/adt/abapgit/repos/{repoKey}/branches/{encodedBranch}`
- **CSRF:** Required

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `create` | boolean | No | `true` to create new branch |

---

## 23. Feeds, Traces & System Info

### 23.1 List Feeds

- **Method:** GET
- **URL:** `/sap/bc/adt/feeds`
- **Accept:** `application/atom+xml;type=feed`

### 23.2 Runtime Dumps

- **Method:** GET
- **URL:** `/sap/bc/adt/runtime/dumps`
- **Accept:** `application/atom+xml;type=feed`

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `$query` | string | No | Filter expression |

### 23.3 List Traces

- **Method:** GET
- **URL:** `/sap/bc/adt/runtime/traces/abaptraces`

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `user` | string | Yes | Username (uppercase) |

### 23.4 Trace Hit List

- **Method:** GET
- **URL:** `/sap/bc/adt/runtime/traces/abaptraces/{id}/hitlist`

### 23.5 System Users

- **Method:** GET
- **URL:** `/sap/bc/adt/system/users`
- **Accept:** `application/atom+xml;type=feed`

### 23.6 System Information

- **Method:** GET
- **URL:** `/sap/bc/adt/system/information`

### 23.7 Reentrance Ticket

- **Method:** GET
- **URL:** `/sap/bc/adt/security/reentranceticket`
- **Response:** Plain text ticket

### 23.8 Run Class (Console App)

- **Method:** POST
- **URL:** `/sap/bc/adt/oo/classrun/{className}`
- **CSRF:** Required
- **Response:** Plain text output

### 23.9 Object Revisions

- **Method:** GET
- **URL:** `{revisionUrl}` -- derived from object structure links where `rel="http://www.sap.com/adt/relations/versions"`
- **Accept:** `application/atom+xml;type=feed`

---

## Appendix A: Object Type Code Reference

Object type codes follow the format `{PGMID}/{OBJECT}`.

### A.1 Classes

| Code | Description |
|------|-------------|
| `CLAS/OC` | Global class (definition) |
| `CLAS/OCI` | Global class implementation |
| `CLAS/OCL` | Global class local class |
| `CLAS/OCN` | Class N variant |
| `CLAS/OCX` | Class X variant |
| `CLAS/I` | Class include |

### A.2 Interfaces

| Code | Description |
|------|-------------|
| `INTF/OI` | Global interface |
| `INTF/I` | Interface include |

### A.3 Programs

| Code | Description |
|------|-------------|
| `PROG/P` | Program (report) |
| `PROG/I` | Program include |
| `PROG/F` | Program function pool |
| `PROG/PG` | Type group |

### A.4 Function Groups / Modules

| Code | Description |
|------|-------------|
| `FUGR/F` | Function group |
| `FUGR/FF` | Function module |
| `FUGR/FFV` | Function module variant |
| `FUGR/I` | Function group include |

### A.5 Data Dictionary

| Code | Description |
|------|-------------|
| `DEVC/K` | Package |
| `TABL/DT` | Database table |
| `TABL/DS` | Structure |
| `VIEW/DV` | View |
| `DTEL/DE` | Data element |
| `DOMA/DD` | Domain |
| `TTYP/DA` | Table type |
| `SHLP/DH` | Search help |

### A.6 CDS / DDL

| Code | Description |
|------|-------------|
| `DDLS/DF` | DDL source (CDS data definition) |
| `DDLS/STOB` | DDL structured object |
| `DDLX/EX` | DDL metadata extension |
| `DCLS/DL` | DCL source (CDS access control) |
| `DDLA/ADF` | CDS annotation definition |
| `DTEB/DF` | Entity behavior source |
| `DTDC/DF` | Data definition source |

### A.7 Services

| Code | Description |
|------|-------------|
| `SRVB/SVB` | Service binding |
| `SRVD/SRV` | Service definition |

### A.8 Other Types

| Code | Description |
|------|-------------|
| `MSAG/NN` | Message class |
| `TRAN/T` | Transaction |
| `SUSO/B` | Authorization object |
| `ENHS/XB` | Enhancement spot |
| `PINF/KI` | Package interface |

---

## Appendix B: Error Patterns

### B.1 HTTP Status Code Meanings

| Status | Meaning | Action |
|--------|---------|--------|
| 200 | Success | Process response |
| 201 | Created | Object created; `Location` header has URI |
| 202 | Accepted | Async operation started; poll `Location` URL |
| 400 | Bad Request | Check request format; may indicate session timeout |
| 401 | Unauthorized | Authentication failed |
| 403 | Forbidden | Check `x-csrf-token: Required` header for token expiry |
| 404 | Not Found | Object does not exist |
| 409 | Conflict | Object locked by another user or version conflict |

### B.2 CSRF Token Error Detection

- HTTP 403 + response header `x-csrf-token: Required` => Token expired; re-fetch
- HTTP 400 + status text `Session timed out` => Session expired; re-login
- HTTP 401 => Authentication failure

### B.3 Error Response XML

```xml
<exc:exception xmlns:exc="http://www.sap.com/adt/exceptions">
  <exc:namespace id="/SAP/BC/ADT"/>
  <exc:type>not_found</exc:type>
  <exc:message lang="EN">Package ZNONEXISTENT does not exist</exc:message>
  <exc:localizedMessage lang="EN">Package ZNONEXISTENT does not exist</exc:localizedMessage>
</exc:exception>
```

### B.4 Object URL Validation Pattern

Valid object URLs match: `/^\/sap\/bc\/adt\/[a-z]+\/[a-zA-Z%\$]?[\w%]+/`

---

## Appendix C: ABAP XML Serialization Format

The ABAP XML serialization format (`asx:abap`) is used by transport data, lock results, node structure responses, and other ABAP-native endpoints.

```xml
<?xml version="1.0" encoding="UTF-8"?>
<asx:abap xmlns:asx="http://www.sap.com/abapxml" version="1.0">
  <asx:values>
    <DATA>
      <FIELD_NAME>{value}</FIELD_NAME>
      <FIELD_NAME/>  <!-- empty field -->
      <NESTED_TABLE>
        <ITEM>
          <FIELD>{value}</FIELD>
        </ITEM>
      </NESTED_TABLE>
    </DATA>
  </asx:values>
</asx:abap>
```

Key characteristics:
- Root element: `<asx:abap version="1.0">`
- Namespace: `http://www.sap.com/abapxml`
- Data wrapper: `<asx:values><DATA>...</DATA></asx:values>`
- Element names: Uppercase, matching ABAP dictionary field names
- Empty elements: Represented as self-closing tags
- Content type when used in Accept/Content-Type headers: `application/vnd.sap.as+xml;charset=UTF-8;dataname={dataName}`

---

## Appendix D: Discovery Document Structure

The ADT discovery document is an Atom Publishing Protocol (APP) service document (RFC 5023). It is the canonical registry of all available ADT services on a given system.

### D.1 Document Structure

```xml
<app:service xmlns:app="http://www.w3.org/2007/app"
             xmlns:atom="http://www.w3.org/2005/Atom"
             xmlns:adtcomp="http://www.sap.com/adt/compatibility">
  <app:workspace>
    <atom:title>{Workspace Title}</atom:title>
    <app:collection href="{collectionUrl}">
      <atom:title>{Collection Title}</atom:title>
      <app:accept>{mediaType}</app:accept>
      <atom:category term="{categoryTerm}"
        scheme="{categoryScheme}"/>
      <adtcomp:templateLinks>
        <adtcomp:templateLink
          rel="{relationUri}"
          template="{uriTemplate}"
          title="{linkTitle}"
          type="{mediaType}"/>
      </adtcomp:templateLinks>
    </app:collection>
  </app:workspace>
</app:service>
```

### D.2 Category Schemes

Collections are organized by category schemes that indicate the functional area:

| Category Scheme | Functional Area |
|----------------|-----------------|
| `http://www.sap.com/adt/categories/oo` | Object-Oriented (classes, interfaces) |
| `http://www.sap.com/adt/categories/packages` | Packages |
| `http://www.sap.com/adt/categories/programs` | Programs |
| `http://www.sap.com/adt/categories/functions` | Functions |
| `http://www.sap.com/adt/categories/ddic` | Data Dictionary |
| `http://www.sap.com/adt/categories/check` | Check runs |
| `http://www.sap.com/adt/categories/activation` | Activation |
| `http://www.sap.com/adt/categories/cts` | Change & Transport System |
| `http://www.sap.com/adt/categories/atc` | ABAP Test Cockpit |
| `http://www.sap.com/adt/categories/abapunit` | ABAP Unit |
| `http://www.sap.com/adt/categories/navigation` | Navigation |
| `http://www.sap.com/adt/categories/repository` | Repository |
| `http://www.sap.com/adt/categories/quickfixes` | Quick fixes |
| `http://www.sap.com/adt/categories/deletion` | Deletion |
| `http://www.sap.com/adt/categories/system` | System info |
| `http://www.sap.com/adt/categories/compatibility` | Compatibility |

### D.3 URI Templates

Template links use RFC 6570 URI Templates. Common patterns:

```
/sap/bc/adt/packages/{packageName}
/sap/bc/adt/oo/classes/{object_name}{?corrNr,lockHandle,version,accessMode,_action}
/sap/bc/adt/ddic/ddl/sources/{object_name}/source/main{?corrNr,lockHandle,version}
/sap/bc/adt/atc/worklists{?checkVariant}
/sap/bc/adt/cts/transportrequests{?targets}
```

### D.4 Using Discovery for Feature Detection

Clients should check the discovery document before calling an endpoint to verify:

1. The endpoint exists on this system version
2. Which media type versions are accepted
3. What URI template parameters are available

This enables graceful degradation when connecting to older SAP systems that may not support newer ADT features.

---

*End of specification.*
