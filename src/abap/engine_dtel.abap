* Data elements (DTEL): DDIF_DTEL_GET / RS_CORR_INSERT + DDIF_DTEL_PUT +
* DDIF_DTEL_ACTIVATE / RS_DD_DELETE_OBJ -- the calls SE11 and abapGit make.
* Engine part: the section after @@DEFINITIONS goes into the class's PRIVATE
* SECTION, the section after @@IMPLEMENTATIONS into its IMPLEMENTATION.
"@@DEFINITIONS
    METHODS dtel_get
      IMPORTING name     TYPE ddobjname
                language TYPE sylangu.
    METHODS dtel_put
      IMPORTING name     TYPE ddobjname
                language TYPE sylangu
                devclass TYPE devclass
                trkorr   TYPE trkorr.
    METHODS dtel_delete
      IMPORTING name   TYPE ddobjname
                trkorr TYPE trkorr.
"@@IMPLEMENTATIONS
  METHOD dtel_get.
    DATA dd04v TYPE dd04v.
    DATA state TYPE ddgotstate.
    DATA xml TYPE xstring.

    CALL FUNCTION 'DDIF_DTEL_GET'
      EXPORTING
        name          = name
        state         = 'A'
        langu         = language
      IMPORTING
        gotstate      = state
        dd04v_wa      = dd04v
      EXCEPTIONS
        illegal_input = 1
        OTHERS        = 2.
    IF sy-subrc <> 0.
      emit_symsg( ).
      emit_rc( sy-subrc ).
      RETURN.
    ENDIF.
    IF dd04v IS INITIAL OR state <> 'A'.
      emit( kind = 'EXISTS' text = `0` ).
      emit_rc( 0 ).
      RETURN.
    ENDIF.
    emit( kind = 'EXISTS' text = `1` ).
    emit( kind = 'META' text = |{ dd04v-as4user } { dd04v-as4date } { dd04v-as4time }| ).

    " Normalization identical to abapGit's ZCL_ABAPGIT_OBJECT_DTEL->serialize.
    CLEAR: dd04v-as4user, dd04v-as4date, dd04v-as4time.
    IF dd04v-refkind = 'D'.
      " values inherited from the domain
      CLEAR: dd04v-datatype, dd04v-leng, dd04v-decimals, dd04v-outputlen,
             dd04v-valexi, dd04v-lowercase, dd04v-signflag, dd04v-convexit,
             dd04v-entitytab.
    ENDIF.
    ASSIGN COMPONENT 'ACTFLAG' OF STRUCTURE dd04v TO FIELD-SYMBOL(<actflag>).
    IF sy-subrc = 0.
      CLEAR <actflag>.
    ENDIF.
    ASSIGN COMPONENT 'RESERVEDTE' OF STRUCTURE dd04v TO FIELD-SYMBOL(<reservedte>).
    IF sy-subrc = 0.
      CLEAR <reservedte>.
    ENDIF.
    IF dd04v-routputlen = ''.
      CLEAR dd04v-routputlen.
    ENDIF.
    IF dd04v-authclass = ''.
      CLEAR dd04v-authclass.
    ENDIF.

    CALL TRANSFORMATION id
      OPTIONS initial_components = 'suppress'
      SOURCE dd04v = dd04v
      RESULT XML xml.
    emit_data( tag = 'XML' xml = xml ).
    emit_rc( 0 ).
  ENDMETHOD.


  METHOD dtel_put.
    DATA dd04v TYPE dd04v.
    DATA rc TYPE sysubrc.
    DATA(xml) = take_payload( ).

    CALL TRANSFORMATION id SOURCE XML xml RESULT dd04v = dd04v.

    IF dd04v-ddtext IS INITIAL.
      emit( kind = 'ERR' text = |Data element { name }: description (DDTEXT) is empty| ).
      emit_rc( 8 ).
      RETURN.
    ENDIF.

    IF corr_insert( object = |DTEL{ name }| object_class = 'DICT'
                    devclass = devclass trkorr = trkorr language = language ) = abap_false.
      RETURN.
    ENDIF.

    dd04v-rollname = name.
    IF dd04v-ddlanguage IS INITIAL.
      dd04v-ddlanguage = language.
    ENDIF.

    CALL FUNCTION 'DDIF_DTEL_PUT'
      EXPORTING
        name              = name
        dd04v_wa          = dd04v
      EXCEPTIONS
        dtel_not_found    = 1
        name_inconsistent = 2
        dtel_inconsistent = 3
        put_failure       = 4
        put_refused       = 5
        OTHERS            = 6.
    IF sy-subrc <> 0.
      emit_symsg( ).
      emit_rc( sy-subrc ).
      RETURN.
    ENDIF.

    CALL FUNCTION 'DDIF_DTEL_ACTIVATE'
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
      emit( kind = 'ERR' text = |Data element { name } could not be activated (DDIC return code { rc }). Does its domain { dd04v-domname } exist and is it active?| ).
      emit_rc( 8 ).
      RETURN.
    ENDIF.
    emit_rc( 0 ).
  ENDMETHOD.


  METHOD dtel_delete.
    DATA objname TYPE rsedd0-ddobjname.
    objname = name.
    CALL FUNCTION 'RS_DD_DELETE_OBJ'
      EXPORTING
        no_ask               = abap_true
        objname              = objname
        objtype              = 'E'
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
