* Programs (PROG): reports and module pools with their screens, GUI status /
* titles and text pool -- mirrors abapGit's ZCL_ABAPGIT_OBJECTS_PROGRAM.
*   read:     READ_PROGDIR, READ REPORT, RS_SCREEN_LIST + RPY_DYNPRO_READ
*             (+ RPY_DYNPRO_READ_NATIVE for abapGit's flag fixes),
*             RS_CUA_INTERNAL_FETCH, READ TEXTPOOL
*   write:    RS_CORR_INSERT, RPY_PROGRAM_INSERT (create), UPDATE_PROGDIR,
*             RPY_DYNPRO_INSERT / RS_SCRP_DELETE, RS_CUA_INTERNAL_WRITE,
*             INSERT TEXTPOOL -- everything saved inactive
*   activate: activate_objects (engine_activate.abap) at the end of the
*             deploy's write run, for every program at once
*   delete:   RS_DELETE_PROGRAM
* Source code of an existing program is written over ADT (lock + PUT), not here.
"@@DEFINITIONS
    METHODS prog_emit_changed
      IMPORTING name     TYPE syrepid
                language TYPE sylangu.
    "! abapGit's ZIF_ABAPGIT_SAP_REPORT=>TY_PROGDIR (component order = XML order).
    TYPES:
      BEGIN OF ty_prog_dir,
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
      END OF ty_prog_dir.
    TYPES ty_prog_spaces TYPE STANDARD TABLE OF i WITH DEFAULT KEY.
    TYPES ty_prog_d021s TYPE STANDARD TABLE OF d021s WITH DEFAULT KEY.
    TYPES ty_prog_d021t TYPE STANDARD TABLE OF d021t WITH DEFAULT KEY.
    "! abapGit's TY_DYNPRO.
    TYPES:
      BEGIN OF ty_prog_dynpro,
        header     TYPE rpy_dyhead,
        containers TYPE dycatt_tab,
        fields     TYPE dyfatc_tab,
        flow_logic TYPE swydyflow,
        spaces     TYPE ty_prog_spaces,
        nat_header TYPE d020s,
        nat_fields TYPE ty_prog_d021s,
        nat_texts  TYPE ty_prog_d021t,
      END OF ty_prog_dynpro.
    TYPES ty_prog_dynpros TYPE STANDARD TABLE OF ty_prog_dynpro WITH DEFAULT KEY.
    "! abapGit's TY_CUA.
    TYPES:
      BEGIN OF ty_prog_cua,
        adm TYPE rsmpe_adm,
        sta TYPE STANDARD TABLE OF rsmpe_stat WITH DEFAULT KEY,
        fun TYPE STANDARD TABLE OF rsmpe_funt WITH DEFAULT KEY,
        men TYPE STANDARD TABLE OF rsmpe_men WITH DEFAULT KEY,
        mtx TYPE STANDARD TABLE OF rsmpe_mnlt WITH DEFAULT KEY,
        act TYPE STANDARD TABLE OF rsmpe_act WITH DEFAULT KEY,
        but TYPE STANDARD TABLE OF rsmpe_but WITH DEFAULT KEY,
        pfk TYPE STANDARD TABLE OF rsmpe_pfk WITH DEFAULT KEY,
        set TYPE STANDARD TABLE OF rsmpe_staf WITH DEFAULT KEY,
        doc TYPE STANDARD TABLE OF rsmpe_atrt WITH DEFAULT KEY,
        tit TYPE STANDARD TABLE OF rsmpe_titt WITH DEFAULT KEY,
        biv TYPE STANDARD TABLE OF rsmpe_buts WITH DEFAULT KEY,
      END OF ty_prog_cua.
    "! abapGit's ZIF_ABAPGIT_LANG_DEFINITIONS=>TY_TPOOL.
    TYPES BEGIN OF ty_prog_tpool.
    INCLUDE TYPE textpool.
    TYPES split TYPE c LENGTH 8.
    TYPES END OF ty_prog_tpool.
    TYPES ty_prog_tpools TYPE STANDARD TABLE OF ty_prog_tpool WITH DEFAULT KEY.
    "! Flow logic of one screen (abapGit keeps it in <prog>.prog.screen_NNNN.abap).
    TYPES:
      BEGIN OF ty_prog_flow,
        screen TYPE dynnr,
        source TYPE string,
      END OF ty_prog_flow.
    TYPES ty_prog_flows TYPE STANDARD TABLE OF ty_prog_flow WITH DEFAULT KEY.
    TYPES ty_prog_dynnrs TYPE STANDARD TABLE OF dynnr WITH DEFAULT KEY.

    METHODS prog_get
      IMPORTING name     TYPE syrepid
                language TYPE sylangu.
    METHODS prog_put
      IMPORTING name          TYPE syrepid
                language      TYPE sylangu
                devclass      TYPE devclass
                trkorr        TYPE trkorr
                create        TYPE abap_bool
                write_progdir TYPE abap_bool
                write_cua     TYPE abap_bool
                write_tpool   TYPE abap_bool.
    "! Emits DATA PENDING: the program's parts that have an inactive version
    "! (REPS, REPT, CUAD, DYNPnnnn), e.g. after a failed activation.
    METHODS prog_emit_pending
      IMPORTING name TYPE syrepid.
    METHODS prog_delete
      IMPORTING name     TYPE syrepid
                devclass TYPE devclass
                trkorr   TYPE trkorr.
    "! abapGit SERIALIZE_DYNPROS: screens in abapGit's normalized form, flow
    "! logic separately. Returns abap_false (after emitting ERR/RC) on failure.
    METHODS prog_read_dynpros
      IMPORTING program        TYPE syrepid
      EXPORTING dynpros        TYPE ty_prog_dynpros
                flows          TYPE ty_prog_flows
      RETURNING VALUE(ok)      TYPE abap_bool.
    "! abapGit DESERIALIZE_DYNPROS (without native dynpros): RPY_DYNPRO_INSERT
    "! with abapGit's force-off rules. Returns abap_false (after emitting) on failure.
    METHODS prog_write_dynpros
      IMPORTING program        TYPE syrepid
                dynpros        TYPE ty_prog_dynpros
                flows          TYPE ty_prog_flows
      RETURNING VALUE(ok)      TYPE abap_bool.
    "! abapGit SERIALIZE_CUA.
    METHODS prog_read_cua
      IMPORTING program        TYPE syrepid
                language       TYPE sylangu
      EXPORTING cua            TYPE ty_prog_cua
      RETURNING VALUE(ok)      TYPE abap_bool.
    "! abapGit DESERIALIZE_CUA (incl. AUTO_CORRECT_CUA_ADM and the SE41 hack).
    METHODS prog_write_cua
      IMPORTING program        TYPE syrepid
                language       TYPE sylangu
                obj_type       TYPE trobjtype
                obj_name       TYPE sobj_name
                cua            TYPE ty_prog_cua
      RETURNING VALUE(ok)      TYPE abap_bool.
    "! Source lines joined with LF plus a final LF (abapGit's ADD_ABAP).
    METHODS prog_lines_to_text
      IMPORTING lines       TYPE string_table
      RETURNING VALUE(text) TYPE string.
    "! The inverse: LF-separated text (final LF optional) into lines.
    METHODS prog_text_to_lines
      IMPORTING text         TYPE string
      RETURNING VALUE(lines) TYPE string_table.
"@@IMPLEMENTATIONS
  METHOD prog_emit_changed.
    " Last changers per part, where SAP keeps one: source and attributes
    " (REPOSRC), text pool (REPOTEXT), GUI status (EUDB, relid CU). Dynpros
    " keep none (D020S), so the workflow treats them as unknown.
    DATA texts TYPE syuname.
    DATA cua TYPE syuname.
    DATA cua_author TYPE syuname.
    emit_changed( part = `SOURCE` user = source_changer( name ) ).
    SELECT SINGLE unam FROM repotext
      WHERE progname = @name AND r3state = 'A' AND language = @language
      INTO @texts.
    emit_changed( part = `TEXTS` user = texts ).
    " VAUTOR is set by the first change; until then AUTOR (the creator) is
    " the last changer.
    SELECT SINGLE vautor, autor FROM eudb
      WHERE relid = 'CU' AND name = @name
      INTO (@cua, @cua_author).
    IF cua IS INITIAL.
      cua = cua_author.
    ENDIF.
    emit_changed( part = `CUA` user = cua ).
  ENDMETHOD.

  METHOD prog_get.
    DATA progdir TYPE progdir.
    DATA dir TYPE ty_prog_dir.
    DATA source TYPE STANDARD TABLE OF abaptxt255 WITH DEFAULT KEY.
    DATA tpool TYPE textpool_table.
    DATA tpool_ext TYPE ty_prog_tpools.
    DATA dynpros TYPE ty_prog_dynpros.
    DATA flows TYPE ty_prog_flows.
    DATA cua TYPE ty_prog_cua.
    DATA bind TYPE abap_trans_srcbind_tab.
    DATA xml TYPE xstring.

    CALL FUNCTION 'READ_PROGDIR'
      EXPORTING
        i_progname = name
        i_state    = 'A'
      IMPORTING
        e_progdir  = progdir
      EXCEPTIONS
        not_exists = 1
        OTHERS     = 2.
    IF sy-subrc <> 0.
      " A program that only exists inactive (e.g. after a failed activation)
      " still exists: report it without content, so a deploy rewrites it.
      CALL FUNCTION 'READ_PROGDIR'
        EXPORTING
          i_progname = name
          i_state    = 'I'
        IMPORTING
          e_progdir  = progdir
        EXCEPTIONS
          not_exists = 1
          OTHERS     = 2.
      IF sy-subrc <> 0.
        emit( kind = 'EXISTS' text = `0` ).
      ELSE.
        emit( kind = 'EXISTS' text = `1` ).
        emit( kind = 'META' text = |{ progdir-unam } { progdir-udat } 000000| ).
        prog_emit_pending( name ).
      ENDIF.
      emit_rc( 0 ).
      RETURN.
    ENDIF.
    emit( kind = 'EXISTS' text = `1` ).
    emit( kind = 'META' text = |{ progdir-unam } { progdir-udat } 000000| ).
    prog_emit_pending( name ).
    prog_emit_changed( name = name language = language ).

    " abapGit READ_PROGDIR: the same fields cleared.
    MOVE-CORRESPONDING progdir TO dir.
    CLEAR: dir-edtx, dir-cnam, dir-cdat, dir-unam, dir-udat, dir-levl, dir-vern,
           dir-rmand, dir-sdate, dir-stime, dir-idate, dir-itime, dir-varcl, dir-state.

    READ REPORT name INTO source STATE 'A'.
    IF sy-subrc <> 0.
      emit( kind = 'ERR' text = |Source of program { name } could not be read.| ).
      emit_rc( 4 ).
      RETURN.
    ENDIF.

    IF dir-subc = '1' OR dir-subc = 'M'.
      IF prog_read_dynpros( EXPORTING program = name IMPORTING dynpros = dynpros flows = flows ) = abap_false.
        RETURN.
      ENDIF.
      IF prog_read_cua( EXPORTING program = name language = language IMPORTING cua = cua ) = abap_false.
        RETURN.
      ENDIF.
    ENDIF.

    READ TEXTPOOL name INTO tpool LANGUAGE language STATE 'A'.
    READ TABLE tpool WITH KEY id = 'R' ASSIGNING FIELD-SYMBOL(<title>).
    IF sy-subrc = 0 AND <title>-key = '' AND <title>-length = 0.
      DELETE tpool INDEX sy-tabix.
    ENDIF.
    " abapGit ADD_TPOOL: selection texts keep their first 8 characters in SPLIT.
    LOOP AT tpool ASSIGNING FIELD-SYMBOL(<in>).
      APPEND INITIAL LINE TO tpool_ext ASSIGNING FIELD-SYMBOL(<out>).
      MOVE-CORRESPONDING <in> TO <out>.
      IF <out>-id = 'S'.
        <out>-split = <out>-entry.
        <out>-entry = <out>-entry+8.
      ENDIF.
    ENDLOOP.

    " abapGit's XML output skips initial values, so do the same.
    APPEND VALUE #( name = 'PROGDIR' value = REF #( dir ) ) TO bind.
    IF dynpros IS NOT INITIAL.
      APPEND VALUE #( name = 'DYNPROS' value = REF #( dynpros ) ) TO bind.
    ENDIF.
    IF cua IS NOT INITIAL.
      APPEND VALUE #( name = 'CUA' value = REF #( cua ) ) TO bind.
    ENDIF.
    IF tpool_ext IS NOT INITIAL.
      APPEND VALUE #( name = 'TPOOL' value = REF #( tpool_ext ) ) TO bind.
    ENDIF.
    CALL TRANSFORMATION id
      OPTIONS initial_components = 'suppress'
      SOURCE (bind)
      RESULT XML xml.
    emit_data( tag = 'XML' xml = xml ).
    emit_data( tag = 'SOURCE' xml = cl_abap_codepage=>convert_to(
      prog_lines_to_text( VALUE #( FOR line IN source ( CONV #( line-line ) ) ) ) ) ).
    LOOP AT flows ASSIGNING FIELD-SYMBOL(<flow>).
      emit_data( tag = |SCREEN_{ <flow>-screen }| xml = cl_abap_codepage=>convert_to( <flow>-source ) ).
    ENDLOOP.
    emit_rc( 0 ).
  ENDMETHOD.


  METHOD prog_read_dynpros.
    DATA d020s TYPE STANDARD TABLE OF d020s WITH DEFAULT KEY.
    DATA header TYPE rpy_dyhead.
    DATA containers TYPE dycatt_tab.
    DATA fields TYPE dyfatc_tab.
    DATA flow_logic TYPE swydyflow.
    DATA fieldlist_int TYPE ty_prog_d021s.
    DATA texts TYPE ty_prog_d021t.
    DATA box_coln TYPE i.
    DATA text_coln TYPE i.
    " Flag values from include MSEUSBIT (abapGit #2746).
    CONSTANTS flg1ddf TYPE x VALUE '20'.
    CONSTANTS flg1edt TYPE x VALUE '80'.
    CONSTANTS flg3fku TYPE x VALUE '08'.
    CONSTANTS flg3for TYPE x VALUE '04'.
    CONSTANTS flg3fdu TYPE x VALUE '02'.

    CLEAR: dynpros, flows.
    CALL FUNCTION 'RS_SCREEN_LIST'
      EXPORTING
        dynnr     = ''
        progname  = program
      TABLES
        dynpros   = d020s
      EXCEPTIONS
        not_found = 1
        OTHERS    = 2.
    IF sy-subrc = 2.
      emit_symsg( ).
      emit_rc( 4 ).
      RETURN.
    ENDIF.
    SORT d020s BY dnum ASCENDING.

    " Generated selection screens are skipped, as in abapGit.
    LOOP AT d020s ASSIGNING FIELD-SYMBOL(<d020s>)
        WHERE type <> 'S' AND type <> 'W' AND type <> 'J' AND NOT dnum IS INITIAL.
      CLEAR: header, containers, fields, flow_logic, fieldlist_int, texts.
      CALL FUNCTION 'RPY_DYNPRO_READ'
        EXPORTING
          progname             = program
          dynnr                = <d020s>-dnum
        IMPORTING
          header               = header
        TABLES
          containers           = containers
          fields_to_containers = fields
          flow_logic           = flow_logic
        EXCEPTIONS
          cancelled            = 1
          not_found            = 2
          permission_error     = 3
          OTHERS               = 4.
      IF sy-subrc <> 0.
        emit_symsg( ).
        emit( kind = 'ERR' text = |Screen { <d020s>-dnum } of { program } could not be read.| ).
        emit_rc( 4 ).
        RETURN.
      ENDIF.

      CALL FUNCTION 'RPY_DYNPRO_READ_NATIVE'
        EXPORTING
          progname   = program
          dynnr      = <d020s>-dnum
        TABLES
          fieldlist  = fieldlist_int
          fieldtexts = texts.

      IF header-type CA 'IN'.
        READ TABLE fieldlist_int TRANSPORTING NO FIELDS WITH KEY fill = 'X'.
        IF sy-subrc = 0.
          emit( kind = 'ERR' text = |Screen { <d020s>-dnum } of { program } is a native dynpro (e.g. with a splitter control). | &&
                                    |abapGit stores it as NAT_HEADER/NAT_FIELDS, which erpl-adt does not support.| ).
          emit_rc( 4 ).
          RETURN.
        ENDIF.
      ENDIF.

      LOOP AT fields ASSIGNING FIELD-SYMBOL(<field>).
        " OUTPUTSTYLE is NUMC; '  ' would not survive the XML conversion.
        ASSIGN COMPONENT 'OUTPUTSTYLE' OF STRUCTURE <field> TO FIELD-SYMBOL(<outputstyle>).
        IF sy-subrc = 0 AND <outputstyle> = '  '.
          CLEAR <outputstyle>.
        ENDIF.
        " abapGit #2746: FOREIGNKEY as SAPLWBSCREEN derives it.
        READ TABLE fieldlist_int ASSIGNING FIELD-SYMBOL(<int>) WITH KEY fnam = <field>-name.
        IF sy-subrc = 0.
          IF <int>-flg1 O flg1ddf AND <int>-flg3 O flg3for AND <int>-flg3 Z flg3fdu AND <int>-flg3 Z flg3fku.
            <field>-foreignkey = 'X'.
          ELSE.
            CLEAR <field>-foreignkey.
          ENDIF.
        ENDIF.
        IF <field>-from_dict = abap_true AND <field>-modific <> 'F' AND <field>-modific <> 'X'.
          CLEAR <field>-text.
        ENDIF.
      ENDLOOP.

      " abapGit FIX_BUTTON_RIGHT_VISLENGTH.
      LOOP AT fields ASSIGNING <field>
          WHERE butt_right = abap_true AND ( type = 'CHECK' OR type = 'RADIO' ).
        CLEAR: box_coln, text_coln.
        LOOP AT fieldlist_int ASSIGNING <int> WHERE fnam = <field>-name AND ( fill = 'A' OR fill = 'C' ).
          IF <int>-flg1 O flg1edt.
            box_coln = <int>-coln.
          ELSE.
            text_coln = <int>-coln.
          ENDIF.
        ENDLOOP.
        IF text_coln > 0 AND box_coln > text_coln.
          <field>-vislength = box_coln - text_coln + 1.
        ENDIF.
      ENDLOOP.

      LOOP AT containers ASSIGNING FIELD-SYMBOL(<container>).
        IF <container>-c_resize_v = abap_false.
          CLEAR <container>-c_line_min.
        ENDIF.
        IF <container>-c_resize_h = abap_false.
          CLEAR <container>-c_coln_min.
        ENDIF.
      ENDLOOP.

      APPEND VALUE #( header = header containers = containers fields = fields ) TO dynpros.
      APPEND VALUE #( screen = header-screen
                      source = prog_lines_to_text( VALUE #( FOR flow IN flow_logic ( CONV #( flow-line ) ) ) ) ) TO flows.
    ENDLOOP.
    ok = abap_true.
  ENDMETHOD.


  METHOD prog_read_cua.
    CLEAR cua.
    CALL FUNCTION 'RS_CUA_INTERNAL_FETCH'
      EXPORTING
        program         = program
        language        = language
        state           = 'A'
      IMPORTING
        adm             = cua-adm
      TABLES
        sta             = cua-sta
        fun             = cua-fun
        men             = cua-men
        mtx             = cua-mtx
        act             = cua-act
        but             = cua-but
        pfk             = cua-pfk
        set             = cua-set
        doc             = cua-doc
        tit             = cua-tit
        biv             = cua-biv
      EXCEPTIONS
        not_found       = 1
        unknown_version = 2
        OTHERS          = 3.
    IF sy-subrc > 1.
      emit_symsg( ).
      emit( kind = 'ERR' text = |GUI status / titles of { program } could not be read.| ).
      emit_rc( 4 ).
      RETURN.
    ENDIF.
    ok = abap_true.
  ENDMETHOD.


  METHOD prog_put.
    DATA dir TYPE ty_prog_dir.
    DATA dynpros TYPE ty_prog_dynpros.
    DATA flows TYPE ty_prog_flows.
    DATA delete_screens TYPE ty_prog_dynnrs.
    DATA cua TYPE ty_prog_cua.
    DATA tpool_ext TYPE ty_prog_tpools.
    DATA tpool TYPE textpool_table.
    DATA source_text TYPE string.
    DATA source TYPE STANDARD TABLE OF abaptxt255 WITH DEFAULT KEY.
    DATA progdir TYPE progdir.
    DATA title TYPE rglif-title.
    DATA(xml) = take_payload( ).

    CALL TRANSFORMATION id SOURCE XML xml
      RESULT progdir        = dir
             dynpros        = dynpros
             flows          = flows
             delete_screens = delete_screens
             cua            = cua
             tpool          = tpool_ext
             source         = source_text.

    " abapGit READ_TPOOL: selection texts get their SPLIT prefix back.
    LOOP AT tpool_ext ASSIGNING FIELD-SYMBOL(<in>).
      APPEND INITIAL LINE TO tpool ASSIGNING FIELD-SYMBOL(<out>).
      MOVE-CORRESPONDING <in> TO <out>.
      IF <out>-id = 'S'.
        CONCATENATE <in>-split <in>-entry INTO <out>-entry RESPECTING BLANKS.
      ENDIF.
    ENDLOOP.

    " R3TR PROG on the request first (otherwise several LIMUs), as abapGit does.
    IF corr_insert( object = name object_class = 'ABAP'
                    devclass = devclass trkorr = trkorr language = language ) = abap_false.
      RETURN.
    ENDIF.

    IF create = abap_true.
      READ TABLE tpool ASSIGNING FIELD-SYMBOL(<title>) WITH KEY id = 'R'.
      IF sy-subrc = 0.
        title = <title>-entry.
        " RPY_PROGRAM_UPDATE bug worked around by abapGit: TTAB's header line is
        " not cleared, so a title length could leak in from another program.
        ASSIGN ('(SAPLSIFP)TTAB') TO FIELD-SYMBOL(<ttab>).
        IF sy-subrc = 0.
          CLEAR <ttab>.
        ENDIF.
      ENDIF.
      source = VALUE #( FOR line IN prog_text_to_lines( source_text ) ( line = line ) ).
      CALL FUNCTION 'RPY_PROGRAM_INSERT'
        EXPORTING
          development_class = devclass
          program_name      = name
          program_type      = dir-subc
          title_string      = title
          transport_number  = trkorr
          save_inactive     = 'I'
          suppress_dialog   = abap_true
          uccheck           = dir-uccheck
        TABLES
          source_extended   = source
        EXCEPTIONS
          already_exists    = 1
          cancelled         = 2
          name_not_allowed  = 3
          permission_error  = 4
          OTHERS            = 5.
      IF sy-subrc <> 0.
        emit_symsg( ).
        emit_rc( sy-subrc ).
        RETURN.
      ENDIF.
    ENDIF.

    IF create = abap_true OR write_progdir = abap_true.
      " abapGit UPDATE_PROGDIR, on the inactive version the source write created.
      CALL FUNCTION 'READ_PROGDIR'
        EXPORTING
          i_progname = name
          i_state    = 'I'
        IMPORTING
          e_progdir  = progdir
        EXCEPTIONS
          not_exists = 1
          OTHERS     = 2.
      IF sy-subrc <> 0.
        emit( kind = 'ERR' text = |Program { name } has no inactive version to update the attributes of.| ).
        emit_rc( 4 ).
        RETURN.
      ENDIF.
      progdir-ldbname = dir-ldbname.
      progdir-dbna    = dir-dbna.
      progdir-dbapl   = dir-dbapl.
      progdir-rload   = dir-rload.
      progdir-fixpt   = dir-fixpt.
      progdir-appl    = dir-appl.
      progdir-rstat   = dir-rstat.
      progdir-uccheck = dir-uccheck.
      progdir-sqlx    = dir-sqlx.
      progdir-clas    = dir-clas.
      progdir-secu    = dir-secu.
      " Not in abapGit's UPDATE_PROGDIR: without it a changed program type
      " (e.g. 1 -> M) would never reach the system and always show as a diff.
      IF dir-subc IS NOT INITIAL.
        progdir-subc = dir-subc.
      ENDIF.
      CALL FUNCTION 'UPDATE_PROGDIR'
        EXPORTING
          i_progdir    = progdir
          i_progname   = progdir-name
          i_state      = progdir-state
        EXCEPTIONS
          not_executed = 1
          OTHERS       = 2.
      IF sy-subrc <> 0.
        emit_symsg( ).
        emit( kind = 'ERR' text = |The attributes of program { name } could not be updated (UPDATE_PROGDIR).| ).
        emit_rc( sy-subrc ).
        RETURN.
      ENDIF.
    ENDIF.

    IF prog_write_dynpros( program = name dynpros = dynpros flows = flows ) = abap_false.
      RETURN.
    ENDIF.

    LOOP AT delete_screens INTO DATA(dynnr).
      CALL FUNCTION 'RS_SCRP_DELETE'
        EXPORTING
          dynnr                  = dynnr
          progname               = name
          with_popup             = abap_false
        EXCEPTIONS
          enqueued_by_user       = 1
          enqueue_system_failure = 2
          not_executed           = 3
          not_exists             = 4
          no_modify_permission   = 5
          popup_canceled         = 6
          OTHERS                 = 7.
      IF sy-subrc <> 0 AND sy-subrc <> 4.
        emit_symsg( ).
        emit( kind = 'ERR' text = |Screen { dynnr } of { name } could not be deleted.| ).
        emit_rc( sy-subrc ).
        RETURN.
      ENDIF.
    ENDLOOP.

    IF write_cua = abap_true.
      IF prog_write_cua( program = name language = language obj_type = 'PROG'
                         obj_name = CONV #( name ) cua = cua ) = abap_false.
        RETURN.
      ENDIF.
    ENDIF.

    IF write_tpool = abap_true.
      " abapGit DESERIALIZE_TEXTPOOL (main language: saved inactive, REPT activated).
      IF tpool IS INITIAL.
        DELETE TEXTPOOL name LANGUAGE language STATE 'I'.
      ELSE.
        INSERT TEXTPOOL name FROM tpool LANGUAGE language STATE 'I'.
        IF sy-subrc <> 0.
          emit( kind = 'ERR' text = |The text pool of { name } could not be saved (INSERT TEXTPOOL).| ).
          emit_rc( 4 ).
          RETURN.
        ENDIF.
      ENDIF.
    ENDIF.
    emit_rc( 0 ).
  ENDMETHOD.


  METHOD prog_write_dynpros.
    " '/' tells RPY_DYNPRO_INSERT to switch a flag off instead of deriving it.
    CONSTANTS force_off TYPE c LENGTH 1 VALUE '/'.
    DATA dynpro TYPE ty_prog_dynpro.

    LOOP AT dynpros INTO dynpro.
      dynpro-header-program = program.
      READ TABLE flows ASSIGNING FIELD-SYMBOL(<flow>) WITH KEY screen = dynpro-header-screen.
      IF sy-subrc = 0.
        dynpro-flow_logic = VALUE #( FOR line IN prog_text_to_lines( <flow>-source ) ( line = line ) ).
      ENDIF.

      LOOP AT dynpro-fields ASSIGNING FIELD-SYMBOL(<field>).
        " DDIC field with a parameter ID: the import would switch SET/GET on.
        IF <field>-param_id IS NOT INITIAL AND <field>-from_dict = abap_true.
          IF <field>-set_param IS INITIAL.
            <field>-set_param = force_off.
          ENDIF.
          IF <field>-get_param IS INITIAL.
            <field>-get_param = force_off.
          ENDIF.
        ENDIF.
        " Otherwise 'F' would be taken over and overlap other fields.
        IF <field>-type = 'CHECK' AND <field>-from_dict = abap_true
            AND <field>-text IS INITIAL AND <field>-modific IS INITIAL.
          <field>-modific = 'X'.
        ENDIF.
        " abapGit #2747.
        IF <field>-foreignkey IS INITIAL.
          <field>-foreignkey = force_off.
        ENDIF.
      ENDLOOP.

      CALL FUNCTION 'RPY_DYNPRO_INSERT'
        EXPORTING
          header                 = dynpro-header
          suppress_exist_checks  = abap_true
          suppress_generate      = dynpro-header-no_execute
        TABLES
          containers             = dynpro-containers
          fields_to_containers   = dynpro-fields
          flow_logic             = dynpro-flow_logic
        EXCEPTIONS
          cancelled              = 1
          already_exists         = 2
          program_not_exists     = 3
          not_executed           = 4
          missing_required_field = 5
          illegal_field_value    = 6
          field_not_allowed      = 7
          not_generated          = 8
          illegal_field_position = 9
          OTHERS                 = 10.
      IF sy-subrc <> 0 AND sy-subrc <> 2.
        DATA(subrc) = sy-subrc.
        emit_symsg( ).
        emit( kind = 'ERR' text = |Screen { dynpro-header-screen } of { program } could not be saved (RPY_DYNPRO_INSERT rc { subrc }).| ).
        emit_rc( subrc ).
        RETURN.
      ENDIF.
    ENDLOOP.
    ok = abap_true.
  ENDMETHOD.


  METHOD prog_write_cua.
    DATA tr_key TYPE trkey.
    DATA adm TYPE rsmpe_adm.
    DATA(sta) = cua-sta.
    DATA(fun) = cua-fun.
    DATA(men) = cua-men.
    DATA(mtx) = cua-mtx.
    DATA(act) = cua-act.
    DATA(but) = cua-but.
    DATA(pfk) = cua-pfk.
    DATA(set) = cua-set.
    DATA(doc) = cua-doc.
    DATA(tit) = cua-tit.
    DATA(biv) = cua-biv.

    SELECT SINGLE devclass FROM tadir
      WHERE pgmid = 'R3TR' AND object = @obj_type AND obj_name = @obj_name
      INTO @tr_key-devclass.
    IF sy-subrc <> 0.
      emit( kind = 'ERR' text = |{ obj_type } { obj_name } is not in TADIR; the GUI status cannot be saved.| ).
      emit_rc( 4 ).
      RETURN.
    ENDIF.
    tr_key-obj_type = obj_type.
    tr_key-obj_name = obj_name.
    tr_key-sub_type = 'CUAD'.
    tr_key-sub_name = program.

    " abapGit AUTO_CORRECT_CUA_ADM (#1807): rebuild ADM if it is not numeric.
    adm = cua-adm.
    IF NOT ( adm IS NOT INITIAL
             AND adm-actcode CO ' 0123456789'
             AND adm-mencode CO ' 0123456789'
             AND adm-pfkcode CO ' 0123456789' ).
      LOOP AT act ASSIGNING FIELD-SYMBOL(<act>).
        IF <act>-code+6(14) IS INITIAL AND <act>-code(6) CO '0123456789'.
          adm-actcode = <act>-code.
        ENDIF.
      ENDLOOP.
      LOOP AT men ASSIGNING FIELD-SYMBOL(<men>).
        IF <men>-code+6(14) IS INITIAL AND <men>-code(6) CO '0123456789'.
          adm-mencode = <men>-code.
        ENDIF.
      ENDLOOP.
      LOOP AT pfk ASSIGNING FIELD-SYMBOL(<pfk>).
        IF <pfk>-code+6(14) IS INITIAL AND <pfk>-code(6) CO '0123456789'.
          adm-pfkcode = <pfk>-code.
        ENDIF.
      ENDLOOP.
    ENDIF.

    " SAP note 2159455: RS_CUA_INTERNAL_WRITE only works as if called from SE41.
    sy-tcode = 'SE41'.
    CALL FUNCTION 'RS_CUA_INTERNAL_WRITE'
      EXPORTING
        program   = program
        language  = language
        tr_key    = tr_key
        adm       = adm
        state     = 'I'
      TABLES
        sta       = sta
        fun       = fun
        men       = men
        mtx       = mtx
        act       = act
        but       = but
        pfk       = pfk
        set       = set
        doc       = doc
        tit       = tit
        biv       = biv
      EXCEPTIONS
        not_found = 1
        OTHERS    = 2.
    IF sy-subrc <> 0.
      DATA(subrc) = sy-subrc.
      emit_symsg( ).
      emit( kind = 'ERR' text = |GUI status / titles of { program } could not be saved (RS_CUA_INTERNAL_WRITE rc { subrc }).| ).
      emit_rc( subrc ).
      RETURN.
    ENDIF.
    ok = abap_true.
  ENDMETHOD.


  METHOD prog_emit_pending.
    DATA pending TYPE string.
    DATA padded TYPE c LENGTH 40.
    DATA(pattern) = |{ name }%|.
    padded = name.
    SELECT object, obj_name FROM dwinactiv
      WHERE ( object IN ('REPS','REPT','CUAD') AND obj_name = @name )
         OR ( object = 'DYNP' AND obj_name LIKE @pattern )
      ORDER BY object, obj_name
      INTO TABLE @DATA(inactive).
    LOOP AT inactive ASSIGNING FIELD-SYMBOL(<inactive>).
      IF <inactive>-object = 'DYNP'.
        " DYNP names are <program padded to 40><screen>; skip other programs.
        IF strlen( <inactive>-obj_name ) <> 44 OR <inactive>-obj_name(40) <> padded.
          CONTINUE.
        ENDIF.
        pending = |{ pending } DYNP{ <inactive>-obj_name+40(4) }|.
      ELSE.
        pending = |{ pending } { <inactive>-object }|.
      ENDIF.
    ENDLOOP.
    IF pending IS NOT INITIAL.
      emit_data( tag = 'PENDING' xml = cl_abap_codepage=>convert_to( condense( pending ) ) ).
    ENDIF.
  ENDMETHOD.


  METHOD prog_delete.
    CALL FUNCTION 'RS_DELETE_PROGRAM'
      EXPORTING
        corrnumber                 = trkorr
        program                    = name
        suppress_popup             = abap_true
        mass_delete_call           = abap_true
        tadir_devclass             = devclass
        force_delete_used_includes = abap_true
      EXCEPTIONS
        enqueue_lock               = 1
        object_not_found           = 2
        permission_failure         = 3
        reject_deletion            = 4
        OTHERS                     = 5.
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


  METHOD prog_lines_to_text.
    " Callers convert from CHAR lines, which drops each line's trailing blanks
    " exactly like abapGit's CONCATENATE LINES OF does.
    CONCATENATE LINES OF lines INTO text SEPARATED BY cl_abap_char_utilities=>newline.
    text = text && cl_abap_char_utilities=>newline.
  ENDMETHOD.


  METHOD prog_text_to_lines.
    DATA(body) = text.
    IF strlen( body ) > 0 AND substring( val = body off = strlen( body ) - 1 len = 1 ) = cl_abap_char_utilities=>newline.
      body = substring( val = body len = strlen( body ) - 1 ).
    ENDIF.
    IF text IS INITIAL.
      RETURN.
    ENDIF.
    " SPLIT drops a trailing empty field, so empty lines are kept explicitly.
    DATA(rest) = body.
    DO.
      FIND cl_abap_char_utilities=>newline IN rest MATCH OFFSET DATA(pos).
      IF sy-subrc <> 0.
        APPEND rest TO lines.
        EXIT.
      ENDIF.
      APPEND substring( val = rest len = pos ) TO lines.
      rest = substring( val = rest off = pos + 1 ).
    ENDDO.
  ENDMETHOD.

