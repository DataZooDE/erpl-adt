"! erpl-adt classic-object engine (temporary console helper class).
"!
"! erpl-adt generates one throwaway copy of this class per run: the class
"! name placeholder is replaced, and the generated operation calls are put in
"! at the OPS marker inside RUN_OPS. The class is written to $TMP, activated,
"! run through ADT classrun and deleted again.
"!
"! It only calls SAP's own workbench function modules -- the ones SE51, SE41,
"! SE93 and SE11 use internally -- so CTS recording, enqueue locks, version
"! management and authorization checks behave exactly as in the GUI tools.
"! It never writes repository tables directly.
"!
"! Output protocol (one line per fact, parsed by src/adt/classic_engine.cpp):
"!   @<op> EXISTS 0|1
"!   @<op> META <changed-by> <changed-on> <changed-at>
"!   @<op> DATA <tag> <base64 chunk>      (chunks of one tag concatenate)
"!   @<op> MSG <type> <class> <number> <text>
"!   @<op> ERR <text>                     (activation / check log lines)
"!   @<op> RC <n>                         (last line of every operation)
"!   @<op> EXC <text>                     (unexpected exception)
"!   @END
CLASS zcl_erpl_classic_engine DEFINITION PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    INTERFACES if_oo_adt_classrun.

  PRIVATE SECTION.
    DATA out    TYPE REF TO if_oo_adt_classrun_out.
    DATA op     TYPE string.
    DATA chunks TYPE string_table.

    METHODS run_ops
      RAISING cx_static_check cx_dynamic_check.
    METHODS emit
      IMPORTING kind TYPE string
                text TYPE string OPTIONAL.
    METHODS emit_rc
      IMPORTING rc TYPE i.
    METHODS emit_symsg.
    METHODS emit_data
      IMPORTING tag TYPE string
                xml TYPE xstring.
    METHODS take_payload
      RETURNING VALUE(xml) TYPE xstring.
    METHODS corr_insert
      IMPORTING object       TYPE clike
                object_class TYPE clike
                devclass     TYPE devclass
                trkorr       TYPE trkorr
                language     TYPE sylangu
      RETURNING VALUE(ok)    TYPE abap_bool.

    METHODS doma_get
      IMPORTING name     TYPE ddobjname
                language TYPE sylangu.
    METHODS doma_put
      IMPORTING name     TYPE ddobjname
                language TYPE sylangu
                devclass TYPE devclass
                trkorr   TYPE trkorr.
    METHODS doma_delete
      IMPORTING name   TYPE ddobjname
                trkorr TYPE trkorr.
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
    emit( kind = 'RC' text = |{ rc }| ).
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
      emit_symsg( ).
      emit_rc( sy-subrc ).
      RETURN.
    ENDIF.
    ok = abap_true.
  ENDMETHOD.


  METHOD doma_get.
    DATA dd01v TYPE dd01v.
    DATA dd07v TYPE STANDARD TABLE OF dd07v WITH DEFAULT KEY.
    DATA state TYPE ddgotstate.
    DATA masklen TYPE c LENGTH 4.
    DATA xml TYPE xstring.

    CALL FUNCTION 'DDIF_DOMA_GET'
      EXPORTING
        name          = name
        state         = 'A'
        langu         = language
      IMPORTING
        gotstate      = state
        dd01v_wa      = dd01v
      TABLES
        dd07v_tab     = dd07v
      EXCEPTIONS
        illegal_input = 1
        OTHERS        = 2.
    IF sy-subrc <> 0.
      emit_symsg( ).
      emit_rc( sy-subrc ).
      RETURN.
    ENDIF.
    IF dd01v IS INITIAL OR state <> 'A'.
      emit( kind = 'EXISTS' text = `0` ).
      emit_rc( 0 ).
      RETURN.
    ENDIF.
    emit( kind = 'EXISTS' text = `1` ).
    emit( kind = 'META' text = |{ dd01v-as4user } { dd01v-as4date } { dd01v-as4time }| ).

    " Normalization identical to abapGit's ZCL_ABAPGIT_OBJECT_DOMA->serialize.
    CLEAR: dd01v-as4user, dd01v-as4date, dd01v-as4time, dd01v-appexist.
    ASSIGN COMPONENT 'ACTFLAG' OF STRUCTURE dd01v TO FIELD-SYMBOL(<actflag>).
    IF sy-subrc = 0.
      CLEAR <actflag>.
    ENDIF.
    IF dd01v-authclass = ''.
      CLEAR dd01v-authclass.
    ENDIF.
    masklen = dd01v-masklen.
    IF masklen = '' OR NOT masklen CO '0123456789'.
      CLEAR dd01v-masklen.
    ENDIF.
    DELETE dd07v WHERE appval = abap_true.
    SORT dd07v BY valpos ASCENDING ddlanguage ASCENDING.
    LOOP AT dd07v ASSIGNING FIELD-SYMBOL(<value>).
      CLEAR <value>-domname.
    ENDLOOP.

    CALL TRANSFORMATION id
      OPTIONS initial_components = 'suppress'
      SOURCE dd01v = dd01v dd07v_tab = dd07v
      RESULT XML xml.
    emit_data( tag = 'XML' xml = xml ).
    emit_rc( 0 ).
  ENDMETHOD.


  METHOD doma_put.
    DATA dd01v TYPE dd01v.
    DATA dd07v TYPE STANDARD TABLE OF dd07v WITH DEFAULT KEY.
    DATA rc TYPE sysubrc.
    DATA(xml) = take_payload( ).

    CALL TRANSFORMATION id SOURCE XML xml RESULT dd01v = dd01v dd07v_tab = dd07v.

    IF corr_insert( object = |DOMA{ name }| object_class = 'DICT'
                    devclass = devclass trkorr = trkorr language = language ) = abap_false.
      RETURN.
    ENDIF.

    dd01v-domname = name.
    IF dd01v-ddlanguage IS INITIAL.
      dd01v-ddlanguage = language.
    ENDIF.
    LOOP AT dd07v ASSIGNING FIELD-SYMBOL(<value>).
      <value>-domname = name.
      <value>-valpos = sy-tabix.
      IF <value>-ddlanguage IS INITIAL.
        <value>-ddlanguage = language.
      ENDIF.
    ENDLOOP.

    CALL FUNCTION 'DDIF_DOMA_PUT'
      EXPORTING
        name              = name
        dd01v_wa          = dd01v
      TABLES
        dd07v_tab         = dd07v
      EXCEPTIONS
        doma_not_found    = 1
        name_inconsistent = 2
        doma_inconsistent = 3
        put_failure       = 4
        put_refused       = 5
        OTHERS            = 6.
    IF sy-subrc <> 0.
      emit_symsg( ).
      emit_rc( sy-subrc ).
      RETURN.
    ENDIF.

    " Activate right away: data elements later in the same deploy refer to it.
    CALL FUNCTION 'DDIF_DOMA_ACTIVATE'
      EXPORTING
        name        = name
      IMPORTING
        rc          = rc
      EXCEPTIONS
        not_found   = 1
        put_failure = 2
        OTHERS      = 3.
    IF sy-subrc <> 0.
      emit_symsg( ).
      emit_rc( sy-subrc ).
      RETURN.
    ENDIF.
    IF rc > 4.
      emit( kind = 'ERR' text = |Domain { name } could not be activated (DDIC return code { rc }). Check it in SE11.| ).
      emit_rc( 8 ).
      RETURN.
    ENDIF.
    emit_rc( 0 ).
  ENDMETHOD.


  METHOD doma_delete.
    DATA objname TYPE rsedd0-ddobjname.
    objname = name.
    CALL FUNCTION 'RS_DD_DELETE_OBJ'
      EXPORTING
        no_ask               = abap_true
        objname              = objname
        objtype              = 'D'
      EXCEPTIONS
        not_executed         = 1
        object_not_found     = 2
        object_not_specified = 3
        permission_failure   = 4
        OTHERS               = 5.
    IF sy-subrc = 2.
      emit( kind = 'EXISTS' text = `0` ).
      emit_rc( 0 ).
      RETURN.
    ENDIF.
    IF sy-subrc <> 0.
      emit_symsg( ).
    ENDIF.
    emit_rc( sy-subrc ).
  ENDMETHOD.

ENDCLASS.
