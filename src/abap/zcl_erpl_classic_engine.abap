* erpl-adt classic-object engine (temporary console helper class).
* 
* erpl-adt generates one throwaway copy of this class per run: the class
* name placeholder is replaced, the per-type parts (engine_<type>.abap) are
* inserted at the TYPE_DEFINITIONS / TYPE_IMPLEMENTATIONS markers, and the
* generated operation calls go in at the OPS marker inside RUN_OPS. The class is written to $TMP, activated,
* run through ADT classrun and deleted again.
* 
* It only calls SAP's own workbench function modules -- the ones SE51, SE41,
* SE93 and SE11 use internally -- so CTS recording, enqueue locks, version
* management and authorization checks behave exactly as in the GUI tools.
* It never writes repository tables directly.
* 
* Output protocol (one line per fact, parsed by src/adt/classic_engine.cpp):
*   @<op> EXISTS 0|1
*   @<op> META <changed-by> <changed-on> <changed-at>
*   @<op> CHANGED <part> <user>          (last changer of one part; ? = unknown)
*   @<op> DATA <tag> <base64 chunk>      (chunks of one tag concatenate)
*   @<op> MSG <type> <class> <number> <text>
*   @<op> ERR <text>                     (activation / check log lines)
*   @<op> KIND LOCK|CTS|AUTH|CHECK       (failure class from the FM exception)
*   @<op> RC <n>                         (last line of every operation)
*   @<op> SKIP <owner>                   (not run: an op of <owner> failed)
*   @<op> INACTIVE <owner><TAB><object> <obj_name> (left inactive by activation)
*   @<op> EXC <text>                     (unexpected exception)
*   @END
* 
* Batched runs: every operation runs in its own TRY block (an exception fails
* that operation with RC 99, not the run). An operation can name its owner
* (the deployed object, "PROG ZFOO") and the owners it requires: it is
* skipped when an operation of a required owner failed earlier in the run.
CLASS zcl_erpl_classic_engine DEFINITION PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    INTERFACES if_oo_adt_classrun.

  PRIVATE SECTION.
    DATA out    TYPE REF TO if_oo_adt_classrun_out.
    DATA op     TYPE string.
    DATA chunks TYPE string_table.
    " Batching: owner of the running op, its last RC, and the owners whose
    " ops failed in this run (their dependents are skipped).
    DATA owner   TYPE string.
    DATA last_rc TYPE i.
    DATA failed  TYPE SORTED TABLE OF string WITH UNIQUE KEY table_line.
    DATA op_exc  TYPE REF TO cx_root.

    METHODS run_ops
      RAISING cx_static_check cx_dynamic_check.
    METHODS emit
      IMPORTING kind TYPE string
                text TYPE string OPTIONAL.
    METHODS emit_rc
      IMPORTING rc TYPE i.
    "! Sets the running op's owner; abap_false (after SKIP + RC 1) when an op
    "! of one of the comma-separated `requires` owners failed in this run.
    METHODS op_guard
      IMPORTING op_owner  TYPE string
                requires  TYPE string
      RETURNING VALUE(ok) TYPE abap_bool.
    "! After every op: a failed op marks its owner failed.
    METHODS op_done.
    METHODS emit_symsg.
    METHODS emit_data
      IMPORTING tag TYPE string
                xml TYPE xstring.
    METHODS emit_changed
      IMPORTING part TYPE string
                user TYPE clike.
    METHODS source_changer
      IMPORTING VALUE(program) TYPE progname
      RETURNING VALUE(user) TYPE syuname.
    METHODS take_payload
      RETURNING VALUE(xml) TYPE xstring.
    METHODS corr_insert
      IMPORTING object       TYPE clike
                object_class TYPE clike
                devclass     TYPE devclass
                trkorr       TYPE trkorr
                language     TYPE sylangu
      RETURNING VALUE(ok)    TYPE abap_bool.

    "@@TYPE_DEFINITIONS@@
ENDCLASS.


CLASS zcl_erpl_classic_engine IMPLEMENTATION.

  METHOD if_oo_adt_classrun~main.
    me->out = out.
    TRY.
        run_ops( ).
      CATCH cx_root INTO DATA(lx_root).
        emit( kind = 'EXC' text = lx_root->get_text( ) ).
    ENDTRY.
    out->write( '@END' ).
  ENDMETHOD.


  METHOD run_ops.
    "@@OPS@@
  ENDMETHOD.


  METHOD emit.
    DATA(clean) = replace( val = text sub = cl_abap_char_utilities=>newline with = ` ` occ = 0 ).
    clean = replace( val = clean sub = cl_abap_char_utilities=>cr_lf(1) with = ` ` occ = 0 ).
    out->write( |@{ op } { kind } { clean }| ).
  ENDMETHOD.


  METHOD emit_rc.
    last_rc = rc.
    emit( kind = 'RC' text = |{ rc }| ).
  ENDMETHOD.


  METHOD op_guard.
    owner = op_owner.
    SPLIT requires AT ',' INTO TABLE DATA(needed).
    LOOP AT needed INTO DATA(needed_owner) WHERE table_line IS NOT INITIAL.
      IF line_exists( failed[ table_line = needed_owner ] ).
        emit( kind = 'SKIP' text = needed_owner ).
        emit_rc( 1 ).
        RETURN.
      ENDIF.
    ENDLOOP.
    ok = abap_true.
  ENDMETHOD.


  METHOD op_done.
    IF owner IS NOT INITIAL AND last_rc <> 0.
      INSERT owner INTO TABLE failed.
    ENDIF.
    CLEAR: owner, last_rc.
  ENDMETHOD.


  METHOD emit_symsg.
    " Snapshot sy-msg* first: MESSAGE ... INTO overwrites sy-msgty.
    DATA(msgid) = sy-msgid.
    DATA(msgty) = sy-msgty.
    DATA(msgno) = sy-msgno.
    DATA text TYPE string.
    IF msgid IS INITIAL.
      RETURN.
    ENDIF.
    MESSAGE ID msgid TYPE 'S' NUMBER msgno
      WITH sy-msgv1 sy-msgv2 sy-msgv3 sy-msgv4 INTO text.
    emit( kind = 'MSG' text = |{ msgty } { msgid } { msgno } { text }| ).
  ENDMETHOD.


  METHOD emit_data.
    DATA(b64) = cl_web_http_utility=>encode_x_base64( xml ).
    DATA(offset) = 0.
    WHILE offset < strlen( b64 ).
      DATA(len) = nmin( val1 = 200 val2 = strlen( b64 ) - offset ).
      emit( kind = |DATA { tag }| text = substring( val = b64 off = offset len = len ) ).
      offset = offset + len.
    ENDWHILE.
  ENDMETHOD.


  METHOD emit_changed.
    IF user IS INITIAL.
      emit( kind = 'CHANGED' text = |{ part } ?| ).
    ELSE.
      emit( kind = 'CHANGED' text = |{ part } { user }| ).
    ENDIF.
  ENDMETHOD.


  METHOD source_changer.
    " Last changer of an active program or include, as SE38/SE37 record it.
    SELECT SINGLE unam FROM reposrc
      WHERE progname = @program AND r3state = 'A'
      INTO @user.
  ENDMETHOD.


  METHOD take_payload.
    xml = cl_web_http_utility=>decode_x_base64( concat_lines_of( chunks ) ).
    CLEAR chunks.
  ENDMETHOD.


  METHOD corr_insert.
    " Same call abapGit and the workbench tools make: creates the TADIR entry
    " for a new object and records it on the transport request (or checks
    " that a local package needs none). Errors such as "object locked in
    " request X of user Y" come back as T100 messages.
    CALL FUNCTION 'RS_CORR_INSERT'
      EXPORTING
        object              = object
        object_class        = object_class
        devclass            = devclass
        korrnum             = trkorr
        master_language     = language
        mode                = 'I'
        global_lock         = abap_true
        suppress_dialog     = abap_true
      EXCEPTIONS
        cancelled           = 1
        permission_failure  = 2
        unknown_objectclass = 3
        OTHERS              = 4.
    IF sy-subrc <> 0.
      DATA(rc) = sy-subrc.
      emit_symsg( ).
      " The verdict for the exit code comes from the exception, not the text.
      emit( kind = 'KIND' text = COND string( WHEN rc = 2 THEN `AUTH` ELSE `CTS` ) ).
      emit_rc( rc ).
      RETURN.
    ENDIF.
    ok = abap_true.
  ENDMETHOD.


  "@@TYPE_IMPLEMENTATIONS@@

ENDCLASS.
