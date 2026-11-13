* Activation of everything a deploy saved inactive (REPS, DYNP, CUAD, REPT,
* ...), in ONE call of RS_WORKING_OBJECTS_ACTIVATE -- the SE80 worklist
* activation -- at the end of the run that wrote the objects.
*   payload: one line per working object, "<owner> TAB <object> TAB <obj_name>"
*            (owner = the deployed object, e.g. "PROG ZFOO"; obj_name as in
*            DWINACTIV, a DYNP is the program padded to 40 plus the screen)
* Entries whose owner failed earlier in the run are not activated. After the
* call, every entry that is still in DWINACTIV is reported as
* "INACTIVE <owner> TAB <object> <obj_name>" and marks its owner failed, so
* operations that require it (e.g. a transaction of the program) are skipped.
"@@DEFINITIONS
    TYPES:
      BEGIN OF ty_act_entry,
        owner    TYPE string,
        object   TYPE dwinactiv-object,
        obj_name TYPE dwinactiv-obj_name,
      END OF ty_act_entry,
      ty_act_entries TYPE STANDARD TABLE OF ty_act_entry WITH DEFAULT KEY.
    METHODS activate_objects.
"@@IMPLEMENTATIONS
  METHOD activate_objects.
    DATA entries TYPE ty_act_entries.
    DATA objects_tab TYPE STANDARD TABLE OF dwinactiv WITH DEFAULT KEY.
    DATA checklist TYPE REF TO cl_wb_checklist.
    DATA errors TYPE swbme_error_tab.
    DATA entry_owner TYPE string.
    DATA entry_object TYPE string.
    DATA entry_name TYPE string.

    DATA(text) = cl_abap_codepage=>convert_from( take_payload( ) ).
    SPLIT text AT cl_abap_char_utilities=>newline INTO TABLE DATA(lines).
    LOOP AT lines INTO DATA(line) WHERE table_line IS NOT INITIAL.
      SPLIT line AT cl_abap_char_utilities=>horizontal_tab
        INTO entry_owner entry_object entry_name.
      IF line_exists( failed[ table_line = entry_owner ] ).
        CONTINUE.  " its write failed in this run: nothing valid to activate
      ENDIF.
      APPEND VALUE #( owner = entry_owner object = entry_object obj_name = entry_name )
        TO entries.
      APPEND VALUE #( object = entry_object obj_name = entry_name ) TO objects_tab.
    ENDLOOP.
    IF objects_tab IS INITIAL.
      emit_rc( 0 ).
      RETURN.
    ENDIF.

    " The writes of this run are committed first, as after a save in SE80.
    COMMIT WORK AND WAIT.

    CALL FUNCTION 'RS_WORKING_OBJECTS_ACTIVATE'
      EXPORTING
        activate_ddic_objects  = abap_false
        with_popup             = abap_false
        ui_decoupled           = abap_true
      IMPORTING
        p_checklist            = checklist
      TABLES
        objects                = objects_tab
      EXCEPTIONS
        excecution_error       = 1
        cancelled              = 2
        insert_into_corr_error = 3
        OTHERS                 = 4.
    DATA(subrc) = sy-subrc.
    DATA(failures) = xsdbool( subrc <> 0 ).
    IF subrc <> 0.
      emit_symsg( ).
    ENDIF.
    IF checklist IS BOUND.
      checklist->get_error_messages( IMPORTING p_error_tab = errors ).
      LOOP AT errors ASSIGNING FIELD-SYMBOL(<error>).
        LOOP AT <error>-mtext INTO DATA(message).
          emit( kind = 'ERR' text = |{ <error>-object_text }: { message }| ).
          failures = abap_true.
        ENDLOOP.
      ENDLOOP.
    ENDIF.

    " What is still inactive did not activate: its owner failed.
    LOOP AT entries ASSIGNING FIELD-SYMBOL(<entry>).
      SELECT SINGLE object FROM dwinactiv
        WHERE object = @<entry>-object AND obj_name = @<entry>-obj_name
        INTO @DATA(still_inactive).
      IF sy-subrc = 0.
        emit( kind = 'INACTIVE'
              text = |{ <entry>-owner }{ cl_abap_char_utilities=>horizontal_tab }{ <entry>-object } { <entry>-obj_name }| ).
        INSERT <entry>-owner INTO TABLE failed.
        failures = abap_true.
      ENDIF.
    ENDLOOP.
    emit_rc( COND #( WHEN subrc <> 0 THEN subrc WHEN failures = abap_true THEN 8 ELSE 0 ) ).
  ENDMETHOD.
