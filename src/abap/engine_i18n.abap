* Translations and documentation in abapGit's in-XML format (the default:
* no USE_LXE in .abapgit.xml). Mirrors abapGit's serializers exactly:
*   DOMA  I18N_LANGS + DD01_TEXTS + DD07_TEXTS   ZCL_ABAPGIT_OBJECT_DOMA=>SERIALIZE_TEXTS
*   DTEL  I18N_LANGS + DD04_TEXTS                ZCL_ABAPGIT_OBJECT_DTEL=>SERIALIZE_TEXTS
*   PROG  I18N_TPOOL                             ZCL_ABAPGIT_OBJECT_PROG=>SERIALIZE_TEXTS
*   docu  LONGTEXTS (DOKIL + THEAD + TLINE)      ZCL_ABAPGIT_LONGTEXTS
* SAP APIs: DDIF_DOMA_GET/PUT/ACTIVATE, DDIF_DTEL_GET/PUT/ACTIVATE (one PUT
* per language, as SE63 and abapGit do), READ/INSERT TEXTPOOL ... LANGUAGE
* (translations are active, no activation), DOCU_READ / DOCU_UPDATE /
* DOCU_DEL (SE61). Every write is recorded with RS_CORR_INSERT on the object.
"@@DEFINITIONS
    TYPES ty_i18n_langus TYPE STANDARD TABLE OF langu WITH DEFAULT KEY.
    TYPES BEGIN OF ty_i18n_dd01_text.
    TYPES   ddlanguage TYPE dd01v-ddlanguage.
    TYPES   ddtext     TYPE dd01v-ddtext.
    TYPES END OF ty_i18n_dd01_text.
    TYPES ty_i18n_dd01_texts TYPE STANDARD TABLE OF ty_i18n_dd01_text WITH DEFAULT KEY.
    TYPES BEGIN OF ty_i18n_dd07_text.
    TYPES   valpos     TYPE dd07v-valpos.
    TYPES   ddlanguage TYPE dd07v-ddlanguage.
    TYPES   domvalue_l TYPE dd07v-domvalue_l.
    TYPES   domvalue_h TYPE dd07v-domvalue_h.
    TYPES   ddtext     TYPE dd07v-ddtext.
    TYPES   domval_ld  TYPE dd07v-domval_ld.
    TYPES   domval_hd  TYPE dd07v-domval_hd.
    TYPES END OF ty_i18n_dd07_text.
    TYPES ty_i18n_dd07_texts TYPE STANDARD TABLE OF ty_i18n_dd07_text WITH DEFAULT KEY.
    TYPES BEGIN OF ty_i18n_dd04_text.
    TYPES   ddlanguage TYPE dd04t-ddlanguage.
    TYPES   ddtext     TYPE dd04t-ddtext.
    TYPES   reptext    TYPE dd04t-reptext.
    TYPES   scrtext_s  TYPE dd04t-scrtext_s.
    TYPES   scrtext_m  TYPE dd04t-scrtext_m.
    TYPES   scrtext_l  TYPE dd04t-scrtext_l.
    TYPES END OF ty_i18n_dd04_text.
    TYPES ty_i18n_dd04_texts TYPE STANDARD TABLE OF ty_i18n_dd04_text WITH DEFAULT KEY.
    "! abapGit's ZIF_ABAPGIT_LANG_DEFINITIONS=>TY_TPOOL / TY_I18N_TPOOL.
    TYPES BEGIN OF ty_i18n_tpool_line.
    INCLUDE TYPE textpool.
    TYPES   split TYPE c LENGTH 8.
    TYPES END OF ty_i18n_tpool_line.
    TYPES ty_i18n_tpool_lines TYPE STANDARD TABLE OF ty_i18n_tpool_line WITH DEFAULT KEY.
    TYPES BEGIN OF ty_i18n_tpool.
    TYPES   language TYPE langu.
    TYPES   textpool TYPE ty_i18n_tpool_lines.
    TYPES END OF ty_i18n_tpool.
    TYPES ty_i18n_tpools TYPE STANDARD TABLE OF ty_i18n_tpool WITH DEFAULT KEY.
    "! abapGit's ZIF_ABAPGIT_LONGTEXTS=>TY_LONGTEXT.
    TYPES BEGIN OF ty_i18n_longtext.
    TYPES   dokil TYPE dokil.
    TYPES   head  TYPE thead.
    TYPES   lines TYPE tline_tab.
    TYPES END OF ty_i18n_longtext.
    TYPES ty_i18n_longtexts TYPE STANDARD TABLE OF ty_i18n_longtext WITH DEFAULT KEY.
    TYPES ty_i18n_language_range TYPE RANGE OF langu.

    "! Translations of a domain: DATA XML with I18N_LANGS, DD01_TEXTS, DD07_TEXTS.
    METHODS i18n_doma_get
      IMPORTING name     TYPE ddobjname
                language TYPE sylangu.
    "! Writes the translations of the payload (one DDIF_DOMA_PUT per language), activates.
    METHODS i18n_doma_put
      IMPORTING name     TYPE ddobjname
                language TYPE sylangu
                devclass TYPE devclass
                trkorr   TYPE trkorr.
    METHODS i18n_dtel_get
      IMPORTING name     TYPE ddobjname
                language TYPE sylangu.
    METHODS i18n_dtel_put
      IMPORTING name     TYPE ddobjname
                language TYPE sylangu
                devclass TYPE devclass
                trkorr   TYPE trkorr.
    "! Text pool translations of a program: DATA XML with I18N_TPOOL.
    METHODS i18n_tpool_get
      IMPORTING name     TYPE progname
                language TYPE sylangu.
    METHODS i18n_tpool_put
      IMPORTING name     TYPE progname
                language TYPE sylangu
                devclass TYPE devclass
                trkorr   TYPE trkorr.
    "! Documentation (SE61) of an object in every language: DATA XML with
    "! <section> (LONGTEXTS, LONGTEXTS_DZ), CHANGED <section> <last changer>.
    METHODS i18n_docu_get
      IMPORTING id      TYPE dokil-id
                object  TYPE dokil-object
                section TYPE string.
    "! Writes the documentation of the payload (DOCU_UPDATE per language) as
    "! documentation <id> of <object>: the identity in the file is ignored.
    METHODS i18n_docu_put
      IMPORTING section     TYPE string
                id          TYPE dokil-id
                object      TYPE dokil-object
                corr_object TYPE string
                corr_class  TYPE string
                devclass    TYPE devclass
                trkorr      TYPE trkorr
                language    TYPE sylangu.
    "! Deletes the documentation of an object in every language (DOCU_DEL).
    METHODS i18n_docu_delete
      IMPORTING id     TYPE dokil-id
                object TYPE dokil-object.
    "! abapGit's system language filter: all languages except the technical
    "! ones of the translation tools (1Q Translation Object Detective, 2Q
    "! pseudo translation).
    METHODS i18n_language_filter
      RETURNING VALUE(filter) TYPE ty_i18n_language_range.
    "! An earlier op of the running op's own object failed in this run (e.g.
    "! the program could not be created): emits RC 1, so its translations are
    "! not written for an object that is not there.
    METHODS i18n_owner_failed
      RETURNING VALUE(failed_before) TYPE abap_bool.
"@@IMPLEMENTATIONS
  METHOD i18n_owner_failed.
    IF owner IS NOT INITIAL AND line_exists( failed[ table_line = owner ] ).
      emit( kind = 'ERR' text = |Not written: an earlier step of { owner } failed.| ).
      emit_rc( 1 ).
      failed_before = abap_true.
    ENDIF.
  ENDMETHOD.


  METHOD i18n_language_filter.
    DATA technical TYPE sylangu.
    LOOP AT VALUE string_table( ( `1Q` ) ( `2Q` ) ) INTO DATA(iso).
      CLEAR technical.
      CALL FUNCTION 'CONVERSION_EXIT_ISOLA_INPUT'
        EXPORTING
          input            = iso
        IMPORTING
          output           = technical
        EXCEPTIONS
          unknown_language = 1
          OTHERS           = 2.
      IF sy-subrc = 0 AND technical IS NOT INITIAL.
        APPEND VALUE #( sign = 'E' option = 'EQ' low = technical ) TO filter.
      ENDIF.
    ENDLOOP.
  ENDMETHOD.


  METHOD i18n_doma_get.
    DATA dd01v TYPE dd01v.
    DATA dd07v TYPE STANDARD TABLE OF dd07v WITH DEFAULT KEY.
    DATA dd01v_lang TYPE dd01v.
    DATA dd07v_lang TYPE STANDARD TABLE OF dd07v WITH DEFAULT KEY.
    DATA langs TYPE ty_i18n_langus.
    DATA dd01_texts TYPE ty_i18n_dd01_texts.
    DATA dd07_texts TYPE ty_i18n_dd07_texts.
    DATA bind TYPE abap_trans_srcbind_tab.
    DATA xml TYPE xstring.
    DATA(filter) = i18n_language_filter( ).

    " The master language values, normalized as in abapGit's serialize.
    CALL FUNCTION 'DDIF_DOMA_GET'
      EXPORTING
        name          = name
        state         = 'A'
        langu         = language
      IMPORTING
        dd01v_wa      = dd01v
      TABLES
        dd07v_tab     = dd07v
      EXCEPTIONS
        illegal_input = 1
        OTHERS        = 2.
    IF sy-subrc <> 0 OR dd01v IS INITIAL.
      emit_rc( 0 ).
      RETURN.
    ENDIF.
    DELETE dd07v WHERE appval = abap_true.
    SORT dd07v BY valpos ASCENDING ddlanguage ASCENDING.

    SELECT DISTINCT ddlanguage AS langu FROM dd01v
      WHERE domname = @name AND ddlanguage IN @filter AND ddlanguage <> @language
      INTO TABLE @langs.
    SELECT DISTINCT ddlanguage AS langu FROM dd07v
      WHERE domname = @name AND ddlanguage IN @filter AND ddlanguage <> @language
      APPENDING TABLE @langs.
    SORT langs.
    DELETE ADJACENT DUPLICATES FROM langs.

    LOOP AT langs INTO DATA(lang).
      DATA(index) = sy-tabix.
      CLEAR: dd01v_lang, dd07v_lang.
      CALL FUNCTION 'DDIF_DOMA_GET'
        EXPORTING
          name          = name
          langu         = lang
        IMPORTING
          dd01v_wa      = dd01v_lang
        TABLES
          dd07v_tab     = dd07v_lang
        EXCEPTIONS
          illegal_input = 1
          OTHERS        = 2.
      IF sy-subrc <> 0.
        DELETE langs INDEX index.
        CONTINUE.
      ENDIF.
      IF dd01v_lang-ddlanguage IS INITIAL.
        dd01v_lang-ddlanguage = lang.
      ENDIF.
      APPEND VALUE #( ddlanguage = dd01v_lang-ddlanguage ddtext = dd01v_lang-ddtext ) TO dd01_texts.
      " Every master language value, with its translation or with empty texts.
      LOOP AT dd07v ASSIGNING FIELD-SYMBOL(<value>) WHERE ddlanguage IS NOT INITIAL.
        APPEND INITIAL LINE TO dd07_texts ASSIGNING FIELD-SYMBOL(<text>).
        READ TABLE dd07v_lang ASSIGNING FIELD-SYMBOL(<translated>)
          WITH KEY ddlanguage = lang domvalue_l = <value>-domvalue_l domvalue_h = <value>-domvalue_h.
        IF sy-subrc = 0.
          MOVE-CORRESPONDING <translated> TO <text>.
        ELSE.
          MOVE-CORRESPONDING <value> TO <text>.
          <text>-ddlanguage = lang.
          CLEAR: <text>-ddtext, <text>-domval_ld, <text>-domval_hd.
        ENDIF.
      ENDLOOP.
    ENDLOOP.

    SORT langs ASCENDING.
    SORT dd01_texts BY ddlanguage ASCENDING.
    SORT dd07_texts BY valpos ASCENDING ddlanguage ASCENDING.
    " abapGit's XML output skips initial values.
    IF langs IS NOT INITIAL.
      APPEND VALUE #( name = 'I18N_LANGS' value = REF #( langs ) ) TO bind.
    ENDIF.
    IF dd01_texts IS NOT INITIAL.
      APPEND VALUE #( name = 'DD01_TEXTS' value = REF #( dd01_texts ) ) TO bind.
    ENDIF.
    IF dd07_texts IS NOT INITIAL.
      APPEND VALUE #( name = 'DD07_TEXTS' value = REF #( dd07_texts ) ) TO bind.
    ENDIF.
    IF bind IS NOT INITIAL.
      CALL TRANSFORMATION id
        OPTIONS initial_components = 'suppress'
        SOURCE (bind)
        RESULT XML xml.
      emit_data( tag = 'XML' xml = xml ).
    ENDIF.
    emit_rc( 0 ).
  ENDMETHOD.


  METHOD i18n_doma_put.
    DATA dd01v TYPE dd01v.
    DATA dd07v TYPE STANDARD TABLE OF dd07v WITH DEFAULT KEY.
    DATA dd01v_lang TYPE dd01v.
    DATA dd07v_lang TYPE STANDARD TABLE OF dd07v WITH DEFAULT KEY.
    DATA langs TYPE ty_i18n_langus.
    DATA dd01_texts TYPE ty_i18n_dd01_texts.
    DATA dd07_texts TYPE ty_i18n_dd07_texts.
    DATA rc TYPE sysubrc.
    DATA(xml) = take_payload( ).
    IF i18n_owner_failed( ) = abap_true.
      RETURN.
    ENDIF.

    CALL TRANSFORMATION id SOURCE XML xml
      RESULT i18n_langs = langs dd01_texts = dd01_texts dd07_texts = dd07_texts.

    IF corr_insert( object = |DOMA{ name }| object_class = 'DICT'
                    devclass = devclass trkorr = trkorr language = language ) = abap_false.
      RETURN.
    ENDIF.

    " The master language version (written and activated before this op).
    CALL FUNCTION 'DDIF_DOMA_GET'
      EXPORTING
        name          = name
        state         = 'A'
        langu         = language
      IMPORTING
        dd01v_wa      = dd01v
      TABLES
        dd07v_tab     = dd07v
      EXCEPTIONS
        illegal_input = 1
        OTHERS        = 2.
    IF sy-subrc <> 0 OR dd01v IS INITIAL.
      emit( kind = 'ERR' text = |Domain { name } has no active version to translate.| ).
      emit_rc( 4 ).
      RETURN.
    ENDIF.
    DELETE dd07v WHERE appval = abap_true.
    SORT dd07v BY valpos ASCENDING.

    " abapGit DESERIALIZE_TEXTS: per language, the master version with the
    " translated texts; values without a translation get empty texts.
    SORT langs.
    LOOP AT langs INTO DATA(lang) WHERE table_line <> language.
      dd01v_lang = dd01v.
      READ TABLE dd01_texts ASSIGNING FIELD-SYMBOL(<dd01_text>) WITH KEY ddlanguage = lang.
      IF sy-subrc <> 0.
        emit( kind = 'ERR' text = |DD01_TEXTS has no text in language { lang }.| ).
        emit_rc( 4 ).
        RETURN.
      ENDIF.
      MOVE-CORRESPONDING <dd01_text> TO dd01v_lang.
      dd07v_lang = dd07v.
      LOOP AT dd07v_lang ASSIGNING FIELD-SYMBOL(<value>).
        DATA(position) = CONV dd07v-valpos( sy-tabix ).
        <value>-domname = name.
        READ TABLE dd07_texts ASSIGNING FIELD-SYMBOL(<dd07_text>)
          WITH KEY ddlanguage = lang domvalue_l = <value>-domvalue_l domvalue_h = <value>-domvalue_h.
        IF sy-subrc = 0.
          MOVE-CORRESPONDING <dd07_text> TO <value>.
        ELSE.
          <value>-ddlanguage = lang.
          CLEAR: <value>-ddtext, <value>-domval_ld, <value>-domval_hd.
        ENDIF.
        <value>-valpos = position.
      ENDLOOP.
      CALL FUNCTION 'DDIF_DOMA_PUT'
        EXPORTING
          name              = name
          dd01v_wa          = dd01v_lang
        TABLES
          dd07v_tab         = dd07v_lang
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
    ENDLOOP.

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
      emit( kind = 'ERR' text = |Domain { name } could not be activated after writing its translations (DDIC return code { rc }).| ).
      emit_rc( 8 ).
      RETURN.
    ENDIF.
    emit_rc( 0 ).
  ENDMETHOD.


  METHOD i18n_dtel_get.
    DATA dd04v TYPE dd04v.
    DATA langs TYPE ty_i18n_langus.
    DATA dd04_texts TYPE ty_i18n_dd04_texts.
    DATA bind TYPE abap_trans_srcbind_tab.
    DATA xml TYPE xstring.
    DATA(filter) = i18n_language_filter( ).

    SELECT DISTINCT ddlanguage AS langu FROM dd04v
      WHERE rollname = @name AND ddlanguage IN @filter AND ddlanguage <> @language
      ORDER BY langu
      INTO TABLE @langs.

    LOOP AT langs INTO DATA(lang).
      DATA(index) = sy-tabix.
      CLEAR dd04v.
      CALL FUNCTION 'DDIF_DTEL_GET'
        EXPORTING
          name          = name
          langu         = lang
        IMPORTING
          dd04v_wa      = dd04v
        EXCEPTIONS
          illegal_input = 1
          OTHERS        = 2.
      IF sy-subrc <> 0 OR dd04v-ddlanguage IS INITIAL.
        DELETE langs INDEX index.
        CONTINUE.
      ENDIF.
      APPEND INITIAL LINE TO dd04_texts ASSIGNING FIELD-SYMBOL(<text>).
      MOVE-CORRESPONDING dd04v TO <text>.
    ENDLOOP.

    SORT langs ASCENDING.
    SORT dd04_texts BY ddlanguage ASCENDING.
    IF langs IS NOT INITIAL.
      APPEND VALUE #( name = 'I18N_LANGS' value = REF #( langs ) ) TO bind.
    ENDIF.
    IF dd04_texts IS NOT INITIAL.
      APPEND VALUE #( name = 'DD04_TEXTS' value = REF #( dd04_texts ) ) TO bind.
    ENDIF.
    IF bind IS NOT INITIAL.
      CALL TRANSFORMATION id
        OPTIONS initial_components = 'suppress'
        SOURCE (bind)
        RESULT XML xml.
      emit_data( tag = 'XML' xml = xml ).
    ENDIF.
    emit_rc( 0 ).
  ENDMETHOD.


  METHOD i18n_dtel_put.
    DATA dd04v TYPE dd04v.
    DATA dd04v_lang TYPE dd04v.
    DATA langs TYPE ty_i18n_langus.
    DATA dd04_texts TYPE ty_i18n_dd04_texts.
    DATA rc TYPE sysubrc.
    DATA(xml) = take_payload( ).
    IF i18n_owner_failed( ) = abap_true.
      RETURN.
    ENDIF.

    CALL TRANSFORMATION id SOURCE XML xml RESULT i18n_langs = langs dd04_texts = dd04_texts.

    IF corr_insert( object = |DTEL{ name }| object_class = 'DICT'
                    devclass = devclass trkorr = trkorr language = language ) = abap_false.
      RETURN.
    ENDIF.

    CALL FUNCTION 'DDIF_DTEL_GET'
      EXPORTING
        name          = name
        state         = 'A'
        langu         = language
      IMPORTING
        dd04v_wa      = dd04v
      EXCEPTIONS
        illegal_input = 1
        OTHERS        = 2.
    IF sy-subrc <> 0 OR dd04v IS INITIAL.
      emit( kind = 'ERR' text = |Data element { name } has no active version to translate.| ).
      emit_rc( 4 ).
      RETURN.
    ENDIF.

    " abapGit DESERIALIZE_TEXTS: the master version with the translated texts.
    SORT langs.
    LOOP AT langs INTO DATA(lang) WHERE table_line <> language.
      dd04v_lang = dd04v.
      READ TABLE dd04_texts ASSIGNING FIELD-SYMBOL(<text>) WITH KEY ddlanguage = lang.
      IF sy-subrc <> 0.
        emit( kind = 'ERR' text = |DD04_TEXTS has no texts in language { lang }.| ).
        emit_rc( 4 ).
        RETURN.
      ENDIF.
      MOVE-CORRESPONDING <text> TO dd04v_lang.
      CALL FUNCTION 'DDIF_DTEL_PUT'
        EXPORTING
          name              = name
          dd04v_wa          = dd04v_lang
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
    ENDLOOP.

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
      emit( kind = 'ERR' text = |Data element { name } could not be activated after writing its translations (DDIC return code { rc }).| ).
      emit_rc( 8 ).
      RETURN.
    ENDIF.
    emit_rc( 0 ).
  ENDMETHOD.


  METHOD i18n_tpool_get.
    DATA langs TYPE ty_i18n_langus.
    DATA tpools TYPE ty_i18n_tpools.
    DATA tpool TYPE textpool_table.
    DATA xml TYPE xstring.
    DATA(filter) = i18n_language_filter( ).

    " D010TINF lists the languages a program's texts are maintained in.
    SELECT DISTINCT language FROM d010tinf
      WHERE r3state = 'A' AND prog = @name AND language <> @language
        AND language IN @filter
      ORDER BY language
      INTO TABLE @langs.
    LOOP AT langs INTO DATA(lang).
      APPEND VALUE #( language = lang ) TO tpools.
    ENDLOOP.

    LOOP AT tpools ASSIGNING FIELD-SYMBOL(<tpool>).
      READ TEXTPOOL name LANGUAGE <tpool>-language INTO tpool.
      " abapGit ADD_TPOOL: selection texts keep their first 8 characters in SPLIT.
      LOOP AT tpool ASSIGNING FIELD-SYMBOL(<in>).
        APPEND INITIAL LINE TO <tpool>-textpool ASSIGNING FIELD-SYMBOL(<out>).
        MOVE-CORRESPONDING <in> TO <out>.
        IF <out>-id = 'S'.
          <out>-split = <out>-entry.
          <out>-entry = <out>-entry+8.
        ENDIF.
      ENDLOOP.
    ENDLOOP.

    IF tpools IS NOT INITIAL.
      CALL TRANSFORMATION id
        OPTIONS initial_components = 'suppress'
        SOURCE i18n_tpool = tpools
        RESULT XML xml.
      emit_data( tag = 'XML' xml = xml ).
    ENDIF.
    emit_rc( 0 ).
  ENDMETHOD.


  METHOD i18n_tpool_put.
    DATA tpools TYPE ty_i18n_tpools.
    DATA tpool TYPE textpool_table.
    DATA(xml) = take_payload( ).
    IF i18n_owner_failed( ) = abap_true.
      RETURN.
    ENDIF.

    CALL TRANSFORMATION id SOURCE XML xml RESULT i18n_tpool = tpools.

    " R3TR PROG on the request, as for the master language text pool.
    IF corr_insert( object = name object_class = 'ABAP'
                    devclass = devclass trkorr = trkorr language = language ) = abap_false.
      RETURN.
    ENDIF.

    DATA(master) = language.
    LOOP AT tpools ASSIGNING FIELD-SYMBOL(<tpool>) WHERE language <> master.
      " abapGit READ_TPOOL: selection texts get their SPLIT prefix back.
      CLEAR tpool.
      LOOP AT <tpool>-textpool ASSIGNING FIELD-SYMBOL(<in>).
        APPEND INITIAL LINE TO tpool ASSIGNING FIELD-SYMBOL(<out>).
        MOVE-CORRESPONDING <in> TO <out>.
        IF <out>-id = 'S'.
          CONCATENATE <in>-split <in>-entry INTO <out>-entry RESPECTING BLANKS.
        ENDIF.
      ENDLOOP.
      " abapGit DESERIALIZE_TEXTPOOL: translations are written active.
      IF tpool IS INITIAL.
        DELETE TEXTPOOL name LANGUAGE <tpool>-language STATE 'A'.
      ELSE.
        INSERT TEXTPOOL name FROM tpool LANGUAGE <tpool>-language STATE 'A'.
        IF sy-subrc <> 0.
          emit( kind = 'ERR' text = |The { <tpool>-language } text pool of { name } could not be saved (INSERT TEXTPOOL).| ).
          emit_rc( 4 ).
          RETURN.
        ENDIF.
      ENDIF.
    ENDLOOP.
    emit_rc( 0 ).
  ENDMETHOD.


  METHOD i18n_docu_get.
    DATA dokil TYPE STANDARD TABLE OF dokil WITH DEFAULT KEY.
    DATA longtexts TYPE ty_i18n_longtexts.
    DATA longtext TYPE ty_i18n_longtext.
    DATA changer TYPE syuname.
    DATA bind TYPE abap_trans_srcbind_tab.
    DATA xml TYPE xstring.

    " abapGit ZCL_ABAPGIT_LONGTEXTS=>READ: every language, by primary key.
    SELECT * FROM dokil
      WHERE id = @id AND object = @object
      ORDER BY PRIMARY KEY
      INTO TABLE @dokil.

    LOOP AT dokil INTO DATA(entry) WHERE txtlines > 0.
      CLEAR longtext.
      longtext-dokil = entry.
      CALL FUNCTION 'DOCU_READ'
        EXPORTING
          id      = entry-id
          langu   = entry-langu
          object  = entry-object
          typ     = entry-typ
          version = entry-version
        IMPORTING
          head    = longtext-head
        TABLES
          line    = longtext-lines.
      IF changer IS INITIAL.
        changer = COND #( WHEN longtext-head-tdluser IS NOT INITIAL
                          THEN longtext-head-tdluser ELSE longtext-head-tdfuser ).
      ENDIF.
      CLEAR: longtext-dokil-dokstate,
             longtext-head-tdfuser, longtext-head-tdfreles,
             longtext-head-tdfdate, longtext-head-tdftime,
             longtext-head-tdluser, longtext-head-tdlreles,
             longtext-head-tdldate, longtext-head-tdltime.
      APPEND longtext TO longtexts.
    ENDLOOP.

    IF longtexts IS NOT INITIAL.
      emit_changed( part = section user = changer ).
      APPEND VALUE #( name = section value = REF #( longtexts ) ) TO bind.
      CALL TRANSFORMATION id
        OPTIONS initial_components = 'suppress'
        SOURCE (bind)
        RESULT XML xml.
      emit_data( tag = 'XML' xml = xml ).
    ENDIF.
    emit_rc( 0 ).
  ENDMETHOD.


  METHOD i18n_docu_put.
    DATA longtexts TYPE ty_i18n_longtexts.
    DATA bind TYPE abap_trans_resbind_tab.
    DATA(xml) = take_payload( ).
    IF i18n_owner_failed( ) = abap_true.
      RETURN.
    ENDIF.

    APPEND VALUE #( name = section value = REF #( longtexts ) ) TO bind.
    CALL TRANSFORMATION id SOURCE XML xml RESULT (bind).

    IF corr_insert( object = corr_object object_class = corr_class
                    devclass = devclass trkorr = trkorr language = language ) = abap_false.
      RETURN.
    ENDIF.

    " abapGit ZCL_ABAPGIT_LONGTEXTS=>DESERIALIZE: written active, a
    " translation (no MASTERLANG) without becoming the original language.
    " The document is the one of this op's object, whatever the file says
    " (a copied file must not write another object's documentation; the
    " workflow refuses such files before anything is written, this is the
    " second line). DOCU_UPDATE keys on HEAD-TDID / TDNAME / TDSPRAS.
    DATA no_masterlang TYPE dokil-masterlang.
    DATA written TYPE string.
    " Every entry is checked before the first is written: no partial write.
    LOOP AT longtexts ASSIGNING FIELD-SYMBOL(<checked>)
         WHERE dokil-typ <> 'E' AND dokil-typ <> 'T'.
      emit( kind = 'ERR' text = |Documentation { id } { object } ({ <checked>-dokil-langu }) has type { <checked>-dokil-typ }; DOCU_UPDATE writes only E and T. Nothing was written.| ).
      emit_rc( 4 ).
      RETURN.
    ENDLOOP.
    LOOP AT longtexts ASSIGNING FIELD-SYMBOL(<longtext>).
      <longtext>-dokil-id     = id.
      <longtext>-dokil-object = object.
      <longtext>-head-tdobject = 'DOKU'.
      <longtext>-head-tdname   = object.
      <longtext>-head-tdid     = id.
      <longtext>-head-tdspras  = <longtext>-dokil-langu.
      no_masterlang = boolc( <longtext>-dokil-masterlang IS INITIAL ).
      CALL FUNCTION 'DOCU_UPDATE'
        EXPORTING
          head          = <longtext>-head
          state         = 'A'
          typ           = <longtext>-dokil-typ
          version       = <longtext>-dokil-version
          no_masterlang = no_masterlang
        TABLES
          line          = <longtext>-lines.
      " DOCU_UPDATE has no exceptions and no return code: check that the
      " document is there now (its index entry DOKIL).
      SELECT SINGLE @abap_true FROM dokil
        WHERE id = @id AND object = @object AND langu = @<longtext>-dokil-langu
          AND typ = @<longtext>-dokil-typ
        INTO @DATA(saved).
      IF sy-subrc <> 0 OR saved <> abap_true.
        emit( kind = 'ERR' text = |The { <longtext>-dokil-langu } documentation { id } { object } was not saved (DOCU_UPDATE)| &&
                                  COND #( WHEN written IS INITIAL THEN `; nothing else was written.`
                                          ELSE |; already written: { written }.| ) ).
        emit_rc( 4 ).
        RETURN.
      ENDIF.
      written = written && COND #( WHEN written IS INITIAL THEN `` ELSE `, ` ) && <longtext>-dokil-langu.
    ENDLOOP.
    emit_rc( 0 ).
  ENDMETHOD.


  METHOD i18n_docu_delete.
    DATA dokil TYPE STANDARD TABLE OF dokil WITH DEFAULT KEY.
    SELECT * FROM dokil
      WHERE id = @id AND object = @object
      ORDER BY PRIMARY KEY
      INTO TABLE @dokil.
    LOOP AT dokil INTO DATA(entry).
      CALL FUNCTION 'DOCU_DEL'
        EXPORTING
          id       = entry-id
          langu    = entry-langu
          object   = entry-object
          typ      = entry-typ
        EXCEPTIONS
          ret_code = 1
          OTHERS   = 2.
      IF sy-subrc <> 0.
        emit_symsg( ).
        emit_rc( sy-subrc ).
        RETURN.
      ENDIF.
    ENDLOOP.
    emit_rc( 0 ).
  ENDMETHOD.
