* Transactions (TRAN): RPY_TRANSACTION_READ / RPY_TRANSACTION_INSERT /
* RPY_TRANSACTION_DELETE -- the calls SE93 and abapGit's
* ZCL_ABAPGIT_OBJECT_TRAN make. RPY_TRANSACTION_INSERT locks the
* transaction and records it on the transport itself (RS_CORR_INSERT with
* transport_number); there is no update API, so an update is delete +
* insert, exactly as in abapGit.
* Engine part: the section after @@DEFINITIONS goes into the class's PRIVATE
* SECTION, the section after @@IMPLEMENTATIONS into its IMPLEMENTATION.
"@@DEFINITIONS
    METHODS tran_get
      IMPORTING name     TYPE tcode
                language TYPE sylangu.
    METHODS tran_put
      IMPORTING name     TYPE tcode
                language TYPE sylangu
                devclass TYPE devclass
                trkorr   TYPE trkorr.
    METHODS tran_delete
      IMPORTING name   TYPE tcode
                trkorr TYPE trkorr.
    METHODS tran_split_parameters
      CHANGING cs_tstcp  TYPE tstcp
               cs_rsstcd TYPE rsstcd
               ct_params TYPE s_param.
"@@IMPLEMENTATIONS
  METHOD tran_get.
    DATA tcodes   TYPE STANDARD TABLE OF tstc WITH DEFAULT KEY.
    DATA gui_attr TYPE STANDARD TABLE OF tstcc WITH DEFAULT KEY.
    DATA ls_tstc  TYPE tstc.
    DATA ls_tstcc TYPE tstcc.
    DATA ls_tstct TYPE tstct.
    DATA ls_tstcp TYPE tstcp.
    DATA lt_tstca TYPE STANDARD TABLE OF tstca WITH DEFAULT KEY.
    DATA lt_i18n  TYPE STANDARD TABLE OF tstct WITH DEFAULT KEY.
    DATA srcbind  TYPE abap_trans_srcbind_tab.
    DATA xml      TYPE xstring.

    CALL FUNCTION 'RPY_TRANSACTION_READ'
      EXPORTING
        transaction      = name
      TABLES
        tcodes           = tcodes
        gui_attributes   = gui_attr
      EXCEPTIONS
        permission_error = 1
        cancelled        = 2
        not_found        = 3
        object_not_found = 4
        OTHERS           = 5.
    IF sy-subrc = 3 OR sy-subrc = 4.
      emit( kind = 'EXISTS' text = `0` ).
      emit_rc( 0 ).
      RETURN.
    ELSEIF sy-subrc <> 0.
      emit_symsg( ).
      emit_rc( sy-subrc ).
      RETURN.
    ENDIF.
    READ TABLE tcodes INDEX 1 INTO ls_tstc.
    IF sy-subrc <> 0.
      emit( kind = 'EXISTS' text = `0` ).
      emit_rc( 0 ).
      RETURN.
    ENDIF.
    READ TABLE gui_attr INDEX 1 INTO ls_tstcc.

    " Same reads as ZCL_ABAPGIT_OBJECT_TRAN->serialize.
    SELECT SINGLE * FROM tstct INTO @ls_tstct
      WHERE sprsl = @language AND tcode = @name.
    SELECT SINGLE * FROM tstcp INTO @ls_tstcp
      WHERE tcode = @name.
    SELECT * FROM tstca INTO TABLE @lt_tstca
      WHERE tcode = @name
      ORDER BY PRIMARY KEY.
    " Translations: abapGit adds them as I18N_TPOOL (without TCODE).
    SELECT sprsl, ttext FROM tstct
      WHERE sprsl <> @language AND tcode = @name
      ORDER BY sprsl
      INTO CORRESPONDING FIELDS OF TABLE @lt_i18n.

    " No META: SAP records no last changer for a transaction (TSTC* have no
    " user field, SE93 writes no version) and the TADIR author is only the
    " creator. The workflow treats the changer as unknown.
    emit( kind = 'EXISTS' text = `1` ).

    " Parts in abapGit's order; initial ones are left out as in abapGit's files.
    IF ls_tstc IS NOT INITIAL.
      APPEND VALUE #( name = 'TSTC' value = REF #( ls_tstc ) ) TO srcbind.
    ENDIF.
    IF ls_tstcc IS NOT INITIAL.
      APPEND VALUE #( name = 'TSTCC' value = REF #( ls_tstcc ) ) TO srcbind.
    ENDIF.
    IF ls_tstct IS NOT INITIAL.
      APPEND VALUE #( name = 'TSTCT' value = REF #( ls_tstct ) ) TO srcbind.
    ENDIF.
    IF ls_tstcp IS NOT INITIAL.
      APPEND VALUE #( name = 'TSTCP' value = REF #( ls_tstcp ) ) TO srcbind.
    ENDIF.
    IF lt_tstca IS NOT INITIAL.
      APPEND VALUE #( name = 'AUTHORIZATIONS' value = REF #( lt_tstca ) ) TO srcbind.
    ENDIF.
    IF lt_i18n IS NOT INITIAL.
      APPEND VALUE #( name = 'I18N_TPOOL' value = REF #( lt_i18n ) ) TO srcbind.
    ENDIF.

    CALL TRANSFORMATION id
      OPTIONS initial_components = 'suppress'
      SOURCE (srcbind)
      RESULT XML xml.
    emit_data( tag = 'XML' xml = xml ).
    emit_rc( 0 ).
  ENDMETHOD.


  METHOD tran_put.
    " CINFO flags, as in ZCL_ABAPGIT_OBJECT_TRAN->deserialize.
    CONSTANTS c_hex_par TYPE x VALUE '02'.
    CONSTANTS c_hex_rep TYPE x VALUE '80'.
    CONSTANTS c_hex_obj TYPE x VALUE '08'.
    DATA ls_tstc   TYPE tstc.
    DATA ls_tstcc  TYPE tstcc.
    DATA ls_tstct  TYPE tstct.
    DATA ls_tstcp  TYPE tstcp.
    DATA ls_rsstcd TYPE rsstcd.
    DATA lt_params TYPE s_param.
    DATA tr_type   TYPE stran_type.
    DATA dynpro    TYPE d020s-dnum.
    DATA html      TYPE s_webgui.
    DATA ewt       TYPE s_ewt.
    DATA existing  TYPE tstc-tcode.
    DATA(xml) = take_payload( ).

    CALL TRANSFORMATION id SOURCE XML xml
      RESULT tstc = ls_tstc tstcc = ls_tstcc tstct = ls_tstct tstcp = ls_tstcp.

    " Force the transaction code (abapGit does the same, for security reasons).
    ls_tstc-tcode = name.
    ls_tstcp-tcode = name.

    IF ls_tstc-cinfo O c_hex_rep.
      tr_type = 'R'.
    ELSEIF ls_tstc-cinfo O c_hex_obj.
      emit( kind = 'ERR' text = |{ name } is an OO transaction; RPY_TRANSACTION_INSERT cannot create it.| ).
      emit_rc( 8 ).
      RETURN.
    ELSEIF ls_tstc-cinfo O c_hex_par.
      IF ls_tstcp-param(1) = '@'.
        tr_type = 'V'.
      ELSE.
        tr_type = 'P'.
      ENDIF.
    ELSE.
      tr_type = 'D'.
    ENDIF.

    IF ls_tstcp IS NOT INITIAL.
      tran_split_parameters( CHANGING cs_tstcp = ls_tstcp cs_rsstcd = ls_rsstcd ct_params = lt_params ).
    ENDIF.

    dynpro = ls_tstc-dypno.
    IF ls_tstcc-s_webgui = '2'.
      ewt = abap_true.
    ELSEIF ls_tstcc-s_webgui IS NOT INITIAL.
      html = ls_tstcc-s_webgui.
    ENDIF.

    " No update API: delete + insert, as abapGit does. Both calls lock the
    " transaction and record it on the transport.
    SELECT SINGLE tcode FROM tstc WHERE tcode = @name INTO @existing.
    IF sy-subrc = 0.
      CALL FUNCTION 'RPY_TRANSACTION_DELETE'
        EXPORTING
          transaction      = name
          transport_number = trkorr
        EXCEPTIONS
          not_excecuted    = 1
          object_not_found = 2
          OTHERS           = 3.
      IF sy-subrc <> 0 AND sy-subrc <> 2.
        emit_symsg( ).
        emit_rc( sy-subrc ).
        RETURN.
      ENDIF.
    ENDIF.

    CALL FUNCTION 'RPY_TRANSACTION_INSERT'
      EXPORTING
        transaction             = name
        program                 = ls_tstc-pgmna
        dynpro                  = dynpro
        language                = language
        development_class       = devclass
        transport_number        = trkorr
        transaction_type        = tr_type
        shorttext               = ls_tstct-ttext
        called_transaction      = ls_rsstcd-call_tcode
        called_transaction_skip = ls_rsstcd-st_skip_1
        variant                 = ls_rsstcd-variant
        cl_independend          = ls_rsstcd-s_ind_vari
        easy_web_transaction    = ewt
        html_enabled            = html
        java_enabled            = ls_tstcc-s_platin
        wingui_enabled          = ls_tstcc-s_win32
        servicefile             = ls_tstcc-s_service
      TABLES
        param_values            = lt_params
      EXCEPTIONS
        cancelled               = 1
        already_exist           = 2
        permission_error        = 3
        name_not_allowed        = 4
        name_conflict           = 5
        illegal_type            = 6
        object_inconsistent     = 7
        db_access_error         = 8
        OTHERS                  = 9.
    IF sy-subrc <> 0.
      DATA(rc) = sy-subrc.
      emit_symsg( ).
      IF existing IS NOT INITIAL.
        emit( kind = 'ERR' text = |{ name } was deleted for the update but could not be inserted again; redeploy it.| ).
      ENDIF.
      IF rc = 3.
        emit( kind = 'KIND' text = `AUTH` ).
      ENDIF.
      emit_rc( rc ).
      RETURN.
    ENDIF.
    emit_rc( 0 ).
  ENDMETHOD.


  METHOD tran_split_parameters.
    " TSTCP-PARAM -> RSSTCD + parameter values: abapGit's split_parameters /
    " shift_param (after subroutine split_parameters in include LSEUKF01),
    " without the OO-transaction branch.
    DATA ls_param TYPE rsparam.
    DATA off      TYPE i.
    DATA fdpos    TYPE i.
    DATA param_beg TYPE i.
    DATA len      TYPE i.

    CLEAR cs_rsstcd-s_vari.
    IF cs_tstcp-param(1) = '@'.             " transaction variant
      cs_rsstcd-s_vari = abap_true.
      IF cs_tstcp-param+1(1) = '@'.
        cs_rsstcd-s_ind_vari = abap_true.
        off = 2.
      ELSE.
        CLEAR cs_rsstcd-s_ind_vari.
        off = 1.
      ENDIF.
      IF cs_tstcp-param CA ' '.
      ENDIF.
      fdpos = sy-fdpos - off.
      IF fdpos > 0.
        cs_rsstcd-call_tcode = cs_tstcp-param+off(fdpos).
        fdpos = sy-fdpos + 1.
        cs_rsstcd-variant = cs_tstcp-param+fdpos.
      ENDIF.
    ELSEIF cs_tstcp-param(1) = '/'.         " parameter transaction
      cs_rsstcd-st_tcode = abap_true.
      cs_rsstcd-st_prog = space.
      IF cs_tstcp-param+1(1) = '*'.
        cs_rsstcd-st_skip_1 = abap_true.
      ELSE.
        CLEAR cs_rsstcd-st_skip_1.
      ENDIF.
      IF cs_tstcp-param CA ' '.
      ENDIF.
      param_beg = sy-fdpos + 1.
      fdpos = sy-fdpos - 2.
      IF fdpos > 0.
        cs_rsstcd-call_tcode = cs_tstcp-param+2(fdpos).
      ENDIF.
      SHIFT cs_tstcp-param BY param_beg PLACES.
    ELSE.                                " report with variant
      cs_rsstcd-st_tcode = space.
      cs_rsstcd-st_prog = abap_true.
      cs_rsstcd-variant = cs_tstcp-param.
    ENDIF.

    " name=value; pairs
    DO 254 TIMES.
      IF cs_tstcp-param = space.
        EXIT.
      ENDIF.
      CLEAR ls_param.
      IF cs_tstcp-param NA '='.
        EXIT.
      ENDIF.
      IF sy-fdpos = 0.
        EXIT.
      ENDIF.
      ls_param-field = cs_tstcp-param(sy-fdpos).
      IF ls_param-field(1) = space.
        SHIFT ls_param-field.
      ENDIF.
      fdpos = sy-fdpos + 1.
      SHIFT cs_tstcp-param BY fdpos PLACES.
      IF cs_tstcp-param CA ';'.
        IF sy-fdpos <> 0.
          ls_param-value = cs_tstcp-param(sy-fdpos).
          IF ls_param-value(1) = space.
            SHIFT ls_param-value.
          ENDIF.
        ENDIF.
        fdpos = sy-fdpos + 1.
        SHIFT cs_tstcp-param BY fdpos PLACES.
      ELSE.
        len = strlen( cs_tstcp-param ).
        ls_param-value = cs_tstcp-param(len).
        IF ls_param-value(1) = space.
          SHIFT ls_param-value.
        ENDIF.
        CLEAR cs_tstcp-param.
      ENDIF.
      APPEND ls_param TO ct_params.
    ENDDO.
  ENDMETHOD.


  METHOD tran_delete.
    CALL FUNCTION 'RPY_TRANSACTION_DELETE'
      EXPORTING
        transaction      = name
        transport_number = trkorr
      EXCEPTIONS
        not_excecuted    = 1
        object_not_found = 2
        OTHERS           = 3.
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
