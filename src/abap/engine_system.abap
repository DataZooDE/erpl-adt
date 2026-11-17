* System facts for guard rails: the logon user (drift protection compares it
* with the last changer of each object) and the namespaces that are
* changeable in this system (TRNSPACE-EDITFLAG = X, maintained in SE03),
* the same rule SE80 applies before it lets anyone create /NS/ objects.
"@@DEFINITIONS
    METHODS system_info.
    "! TADIR package of an object, then its parent packages (TDEVC-PARENTCL):
    "! @op PACKAGE <package> <parent> <grandparent> ...  (empty: no TADIR entry)
    METHODS tadir_package
      IMPORTING VALUE(object) TYPE trobjtype
                VALUE(name)   TYPE sobj_name.
"@@IMPLEMENTATIONS
  METHOD tadir_package.
    DATA devclass TYPE devclass.
    DATA chain TYPE string.
    SELECT SINGLE devclass FROM tadir
      WHERE pgmid = 'R3TR' AND object = @object AND obj_name = @name
      INTO @devclass.
    WHILE devclass IS NOT INITIAL AND sy-index <= 30.
      chain = |{ chain } { devclass }|.
      SELECT SINGLE parentcl FROM tdevc WHERE devclass = @devclass INTO @devclass.
      IF sy-subrc <> 0.
        CLEAR devclass.
      ENDIF.
    ENDWHILE.
    emit( kind = 'PACKAGE' text = condense( chain ) ).
    emit_rc( 0 ).
  ENDMETHOD.

  METHOD system_info.
    emit( kind = 'META' text = |{ sy-uname } { sy-datum } { sy-uzeit }| ).
    SELECT namespace FROM trnspace WHERE editflag = 'X' INTO TABLE @DATA(spaces).
    LOOP AT spaces INTO DATA(changeable).
      emit( kind = 'NS' text = |{ changeable-namespace }| ).
    ENDLOOP.
    emit_rc( 0 ).
  ENDMETHOD.

