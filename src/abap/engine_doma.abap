* Domains (DOMA): DDIF_DOMA_GET / RS_CORR_INSERT + DDIF_DOMA_PUT +
* DDIF_DOMA_ACTIVATE / RS_DD_DELETE_OBJ -- the calls SE11 and abapGit make.
* Engine part: the section after @@DEFINITIONS goes into the class's PRIVATE
* SECTION, the section after @@IMPLEMENTATIONS into its IMPLEMENTATION.
"@@DEFINITIONS
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
"@@IMPLEMENTATIONS
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
    " The deletion is recorded on this request by RS_DD_DELETE_OBJ's own
    " corr_insert (CHANGING corrnum); space for local packages.
    DATA corrnum TYPE e070-trkorr.
    corrnum = trkorr.
    objname = name.
    CALL FUNCTION 'RS_DD_DELETE_OBJ'
      EXPORTING
        no_ask               = abap_true
        objname              = objname
        objtype              = 'D'
      CHANGING
        corrnum              = corrnum
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

