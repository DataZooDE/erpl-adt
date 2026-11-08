* Function groups (FUGR): read side of abapGit's ZCL_ABAPGIT_OBJECT_FUGR.
* RS_FUNCTION_POOL_EXISTS, RS_FUNCTION_POOL_CONTENTS, RPY_FUNCTIONMODULE_READ_NEW,
* RS_GET_ALL_INCLUDES, READ_PROGDIR, READ REPORT / READ TEXTPOOL -- the same
* calls abapGit's serializer makes, with its normalization. Writing goes
* through ADT (the function module source in signature form), see
* src/workflow/abapgit_type_fugr.cpp.
* 
* Output of fugr_get:
*   DATA XML           AREAT / INCLUDES / FUNCTIONS (abapGit <fg>.fugr.xml values)
*   DATA FM:<name>     function module source as abapGit writes it (old style, LF)
*   DATA SRC:<include> include source (LF)
*   DATA DIR:<include> PROGDIR (+ TPOOL) of the include (<fg>.fugr.<include>.xml values)
"@@DEFINITIONS
    METHODS fugr_get
      IMPORTING name     TYPE rs38l-area
                language TYPE sylangu.
    METHODS fugr_emit_text
      IMPORTING tag  TYPE string
                text TYPE string.
    METHODS fugr_strip_generation
      CHANGING source TYPE STANDARD TABLE.
"@@IMPLEMENTATIONS
  METHOD fugr_get.
    TYPES:
      BEGIN OF ty_function,
        funcname          TYPE rs38l_fnam,
        global_flag       TYPE rs38l-global,
        remote_call       TYPE rs38l-remote,
        update_task       TYPE rs38l-utask,
        short_text        TYPE tftit-stext,
        remote_basxml     TYPE rs38l-basxml_enabled,
        rfcscope          TYPE c LENGTH 1,
        rfcvers           TYPE c LENGTH 10,
        import            TYPE STANDARD TABLE OF rsimp WITH DEFAULT KEY,
        changing          TYPE STANDARD TABLE OF rscha WITH DEFAULT KEY,
        export            TYPE STANDARD TABLE OF rsexp WITH DEFAULT KEY,
        tables            TYPE STANDARD TABLE OF rstbl WITH DEFAULT KEY,
        exception         TYPE STANDARD TABLE OF rsexc WITH DEFAULT KEY,
        documentation     TYPE STANDARD TABLE OF rsfdo WITH DEFAULT KEY,
        exception_classes TYPE abap_bool,
      END OF ty_function.
    TYPES:
      BEGIN OF ty_progdir,
        name    TYPE progdir-name,
        state   TYPE progdir-state,
        sqlx    TYPE progdir-sqlx,
        edtx    TYPE progdir-edtx,
        varcl   TYPE progdir-varcl,
        dbapl   TYPE progdir-dbapl,
        dbna    TYPE progdir-dbna,
        clas    TYPE progdir-clas,
        type    TYPE progdir-type,
        occurs  TYPE progdir-occurs,
        subc    TYPE progdir-subc,
        appl    TYPE progdir-appl,
        secu    TYPE progdir-secu,
        cnam    TYPE progdir-cnam,
        cdat    TYPE progdir-cdat,
        unam    TYPE progdir-unam,
        udat    TYPE progdir-udat,
        vern    TYPE progdir-vern,
        levl    TYPE progdir-levl,
        rstat   TYPE progdir-rstat,
        rmand   TYPE progdir-rmand,
        rload   TYPE progdir-rload,
        fixpt   TYPE progdir-fixpt,
        sset    TYPE progdir-sset,
        sdate   TYPE progdir-sdate,
        stime   TYPE progdir-stime,
        idate   TYPE progdir-idate,
        itime   TYPE progdir-itime,
        ldbname TYPE progdir-ldbname,
        uccheck TYPE progdir-uccheck,
      END OF ty_progdir.
    TYPES BEGIN OF ty_tpool.
    INCLUDE TYPE textpool.
    TYPES split TYPE c LENGTH 8.
    TYPES END OF ty_tpool.

    DATA functions TYPE STANDARD TABLE OF ty_function WITH DEFAULT KEY.
    DATA function LIKE LINE OF functions.
    DATA functab TYPE STANDARD TABLE OF rs38l_incl WITH DEFAULT KEY.
    DATA enlfdir TYPE STANDARD TABLE OF enlfdir WITH DEFAULT KEY.
    DATA includes TYPE STANDARD TABLE OF sobj_name WITH DEFAULT KEY.
    DATA tadir_includes TYPE STANDARD TABLE OF sobj_name WITH DEFAULT KEY.
    DATA active_includes TYPE STANDARD TABLE OF progname WITH DEFAULT KEY.
    DATA source TYPE STANDARD TABLE OF rssource WITH DEFAULT KEY.
    DATA new_source TYPE rsfb_source.
    DATA incl_source TYPE STANDARD TABLE OF abaptxt255 WITH DEFAULT KEY.
    DATA textpool TYPE STANDARD TABLE OF textpool WITH DEFAULT KEY.
    DATA tpool TYPE STANDARD TABLE OF ty_tpool WITH DEFAULT KEY.
    DATA sapdir TYPE progdir.
    DATA progdir TYPE ty_progdir.
    DATA areat TYPE tlibt-areat.
    DATA pool TYPE tlibg-area.
    DATA namespace TYPE rs38l-namespace.
    DATA group TYPE rs38l-area.
    DATA program TYPE progname.
    DATA maintview TYPE sobj_name.
    DATA other_group TYPE rs38l-area.
    DATA other_ns TYPE rs38l-namespace.
    DATA split_include TYPE rs38l-include.
    DATA xml TYPE xstring.
    DATA index TYPE i.

    pool = name.
    CALL FUNCTION 'RS_FUNCTION_POOL_EXISTS'
      EXPORTING
        function_pool   = pool
      EXCEPTIONS
        pool_not_exists = 1
        OTHERS          = 2.
    IF sy-subrc = 1.
      emit( kind = 'EXISTS' text = `0` ).
      emit_rc( 0 ).
      RETURN.
    ENDIF.

    CALL FUNCTION 'FUNCTION_INCLUDE_SPLIT'
      EXPORTING
        complete_area = name
      IMPORTING
        namespace     = namespace
        group         = group
      EXCEPTIONS
        OTHERS        = 12.
    IF sy-subrc <> 0.
      emit_symsg( ).
      emit_rc( sy-subrc ).
      RETURN.
    ENDIF.
    program = |{ namespace }SAPL{ group }|.

    emit( kind = 'EXISTS' text = `1` ).
    CALL FUNCTION 'READ_PROGDIR'
      EXPORTING
        i_progname = program
        i_state    = 'A'
      IMPORTING
        e_progdir  = sapdir
      EXCEPTIONS
        OTHERS     = 1.
    emit( kind = 'META' text = |{ sapdir-unam } { sapdir-udat } { sapdir-stime }| ).

    SELECT SINGLE areat FROM tlibt INTO @areat
      WHERE spras = @language AND area = @name.

    " Function modules: ZCL_ABAPGIT_OBJECT_FUGR->functions / serialize_functions.
    CALL FUNCTION 'RS_FUNCTION_POOL_CONTENTS'
      EXPORTING
        function_pool           = name
      TABLES
        functab                 = functab
      EXCEPTIONS
        function_pool_not_found = 1
        OTHERS                  = 2.
    IF sy-subrc <> 0.
      emit_symsg( ).
      emit_rc( sy-subrc ).
      RETURN.
    ENDIF.
    SELECT * FROM enlfdir INTO TABLE @enlfdir WHERE area = @name ORDER BY funcname.
    LOOP AT enlfdir ASSIGNING FIELD-SYMBOL(<enlfdir>).
      TRANSLATE <enlfdir>-funcname TO UPPER CASE.
    ENDLOOP.
    SORT enlfdir BY funcname.
    LOOP AT functab ASSIGNING FIELD-SYMBOL(<functab>).
      index = sy-tabix.
      TRANSLATE <functab> TO UPPER CASE.
      READ TABLE enlfdir WITH KEY funcname = <functab>-funcname TRANSPORTING NO FIELDS.
      IF sy-subrc <> 0.
        DELETE functab INDEX index.
      ENDIF.
    ENDLOOP.
    SORT functab BY funcname.
    DELETE ADJACENT DUPLICATES FROM functab COMPARING funcname.

    LOOP AT functab ASSIGNING <functab>.
      CLEAR: function, source, new_source.
      function-funcname = <functab>-funcname.
      CALL FUNCTION 'RPY_FUNCTIONMODULE_READ_NEW'
        EXPORTING
          functionname            = <functab>-funcname
        IMPORTING
          global_flag             = function-global_flag
          remote_call             = function-remote_call
          update_task             = function-update_task
          short_text              = function-short_text
          remote_basxml_supported = function-remote_basxml
        TABLES
          import_parameter        = function-import
          changing_parameter      = function-changing
          export_parameter        = function-export
          tables_parameter        = function-tables
          exception_list          = function-exception
          documentation           = function-documentation
          source                  = source
        CHANGING
          new_source              = new_source
        EXCEPTIONS
          error_message           = 1
          function_not_found      = 2
          invalid_name            = 3
          OTHERS                  = 4.
      IF sy-subrc = 2.
        CONTINUE.
      ELSEIF sy-subrc <> 0.
        emit_symsg( ).
        emit_rc( sy-subrc ).
        RETURN.
      ENDIF.
      LOOP AT function-documentation ASSIGNING FIELD-SYMBOL(<doc>).
        CLEAR <doc>-index.
      ENDLOOP.
      SELECT SINGLE exten3 FROM enlfdir INTO @function-exception_classes
        WHERE funcname = @<functab>-funcname.
      TRY.
          SELECT SINGLE rfcscope, rfcvers FROM ('TFDIR')
            WHERE funcname = @<functab>-funcname
            INTO CORRESPONDING FIELDS OF @function.
        CATCH cx_sy_dynamic_osql_semantics.
      ENDTRY.
      APPEND function TO functions.

      IF new_source IS NOT INITIAL.
        fugr_strip_generation( CHANGING source = new_source ).
        fugr_emit_text( tag  = |FM:{ <functab>-funcname }|
                        text = concat_lines_of( table = new_source sep = cl_abap_char_utilities=>newline ) ).
      ELSE.
        fugr_strip_generation( CHANGING source = source ).
        fugr_emit_text( tag  = |FM:{ <functab>-funcname }|
                        text = concat_lines_of( table = source sep = cl_abap_char_utilities=>newline ) ).
      ENDIF.
    ENDLOOP.

    " Includes: ZCL_ABAPGIT_OBJECT_FUGR->includes.
    CALL FUNCTION 'RS_GET_ALL_INCLUDES'
      EXPORTING
        program      = program
      TABLES
        includetab   = includes
      EXCEPTIONS
        not_existent = 1
        no_program   = 2
        OTHERS       = 3.
    IF sy-subrc <> 0.
      emit( kind = 'ERR' text = |RS_GET_ALL_INCLUDES failed for { program }| ).
      emit_rc( 8 ).
      RETURN.
    ENDIF.
    LOOP AT functab ASSIGNING <functab>.
      DELETE TABLE includes FROM <functab>-include.
    ENDLOOP.
    maintview = |{ namespace }L{ group }T00|.
    READ TABLE includes WITH KEY table_line = maintview TRANSPORTING NO FIELDS.
    IF sy-subrc <> 0.
      APPEND maintview TO includes.
    ENDIF.
    SORT includes.
    IF includes IS NOT INITIAL.
      SELECT obj_name FROM tadir INTO TABLE @tadir_includes
        FOR ALL ENTRIES IN @includes
        WHERE pgmid = 'R3TR' AND object = 'PROG' AND obj_name = @includes-table_line.
      SELECT progname FROM reposrc INTO TABLE @active_includes
        FOR ALL ENTRIES IN @includes
        WHERE progname = @includes-table_line AND r3state = 'A'.
    ENDIF.
    LOOP AT includes ASSIGNING FIELD-SYMBOL(<include>).
      index = sy-tabix.
      IF <include> CP 'LSVIM*'.
        DELETE includes INDEX index.
        CONTINUE.
      ENDIF.
      READ TABLE tadir_includes WITH KEY table_line = <include> TRANSPORTING NO FIELDS.
      IF sy-subrc = 0.
        DELETE includes INDEX index.
        CONTINUE.
      ENDIF.
      IF strlen( <include> ) = 33 AND <include>+30(3) = 'XTI'.
        DELETE includes INDEX index.
        CONTINUE.
      ENDIF.
      READ TABLE active_includes WITH KEY table_line = <include> TRANSPORTING NO FIELDS.
      IF sy-subrc <> 0.
        DELETE includes INDEX index.
        CONTINUE.
      ENDIF.
      " includes of another function group (abapGit: is_part_of_other_fugr)
      IF <include>(1) = 'L' OR <include>+1 CS '/L'.
        split_include = <include>.
        CLEAR: other_group, other_ns.
        CALL FUNCTION 'FUNCTION_INCLUDE_SPLIT'
          IMPORTING
            namespace = other_ns
            group     = other_group
          CHANGING
            include   = split_include
          EXCEPTIONS
            OTHERS    = 1.
        IF sy-subrc = 0 AND other_group IS NOT INITIAL
           AND |{ other_ns }{ other_group }| <> name.
          DELETE includes INDEX index.
          CONTINUE.
        ENDIF.
      ENDIF.
    ENDLOOP.
    APPEND program TO includes.
    SORT includes.

    LOOP AT includes ASSIGNING <include>.
      CLEAR: sapdir, progdir, incl_source, textpool, tpool.
      CALL FUNCTION 'READ_PROGDIR'
        EXPORTING
          i_progname = <include>
          i_state    = 'A'
        IMPORTING
          e_progdir  = sapdir
        EXCEPTIONS
          OTHERS     = 1.
      MOVE-CORRESPONDING sapdir TO progdir.
      " ZCL_ABAPGIT_SAP_REPORT->read_progdir
      CLEAR: progdir-edtx, progdir-cnam, progdir-cdat, progdir-unam, progdir-udat,
             progdir-levl, progdir-vern, progdir-rmand, progdir-sdate, progdir-stime,
             progdir-idate, progdir-itime, progdir-varcl, progdir-state.
      READ TEXTPOOL <include> INTO textpool LANGUAGE language.
      READ TABLE textpool WITH KEY id = 'R' ASSIGNING FIELD-SYMBOL(<r>).
      IF sy-subrc = 0 AND <r>-key = '' AND <r>-length = 0.
        DELETE textpool INDEX sy-tabix.
      ENDIF.
      LOOP AT textpool ASSIGNING FIELD-SYMBOL(<tp>).
        APPEND INITIAL LINE TO tpool ASSIGNING FIELD-SYMBOL(<tpout>).
        MOVE-CORRESPONDING <tp> TO <tpout>.
        IF <tpout>-id = 'S'.
          <tpout>-split = <tpout>-entry.
          <tpout>-entry = <tpout>-entry+8.
        ENDIF.
      ENDLOOP.
      IF tpool IS INITIAL.
        CALL TRANSFORMATION id
          OPTIONS initial_components = 'suppress'
          SOURCE progdir = progdir
          RESULT XML xml.
      ELSE.
        CALL TRANSFORMATION id
          OPTIONS initial_components = 'suppress'
          SOURCE progdir = progdir tpool = tpool
          RESULT XML xml.
      ENDIF.
      emit_data( tag = |DIR:{ <include> }| xml = xml ).

      READ REPORT <include> INTO incl_source STATE 'A'.
      fugr_strip_generation( CHANGING source = incl_source ).
      fugr_emit_text( tag  = |SRC:{ <include> }|
                      text = concat_lines_of( table = incl_source sep = cl_abap_char_utilities=>newline ) ).
    ENDLOOP.

    CALL TRANSFORMATION id
      OPTIONS initial_components = 'suppress'
      SOURCE areat = areat includes = includes functions = functions
      RESULT XML xml.
    emit_data( tag = 'XML' xml = xml ).
    emit_rc( 0 ).
  ENDMETHOD.


  METHOD fugr_emit_text.
    emit_data( tag = tag
               xml = cl_abap_conv_codepage=>create_out( )->convert(
                       text && cl_abap_char_utilities=>newline ) ).
  ENDMETHOD.


  METHOD fugr_strip_generation.
    " ZCL_ABAPGIT_OBJECTS_PROGRAM->strip_generation_comments (FUGR only)
    FIELD-SYMBOLS <line> TYPE any.
    READ TABLE source INDEX 1 ASSIGNING <line>.
    IF sy-subrc = 0 AND <line> CP '#**regenerated at *'.
      DELETE source INDEX 1.
      RETURN.
    ENDIF.
    IF lines( source ) < 5.
      RETURN.
    ENDIF.
    READ TABLE source INDEX 1 ASSIGNING <line>.
    IF NOT <line> CP '#*---*'.
      RETURN.
    ENDIF.
    READ TABLE source INDEX 2 ASSIGNING <line>.
    IF NOT <line> CP '#**'.
      RETURN.
    ENDIF.
    READ TABLE source INDEX 3 ASSIGNING <line>.
    IF NOT <line> CP '#**generation date:*'.
      RETURN.
    ENDIF.
    READ TABLE source INDEX 4 ASSIGNING <line>.
    IF NOT <line> CP '#**generator version:*'.
      RETURN.
    ENDIF.
    READ TABLE source INDEX 5 ASSIGNING <line>.
    IF NOT <line> CP '#*---*'.
      RETURN.
    ENDIF.
    DELETE source INDEX 4.
    DELETE source INDEX 3.
  ENDMETHOD.
