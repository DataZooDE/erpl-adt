* System facts for guard rails: the logon user (drift protection compares it
* with the last changer of each object) and the namespaces that are
* changeable in this system (TRNSPACE-EDITFLAG = X, maintained in SE03),
* the same rule SE80 applies before it lets anyone create /NS/ objects.
"@@DEFINITIONS
    METHODS system_info.
"@@IMPLEMENTATIONS
  METHOD system_info.
    emit( kind = 'META' text = |{ sy-uname } { sy-datum } { sy-uzeit }| ).
    SELECT namespace FROM trnspace WHERE editflag = 'X' INTO TABLE @DATA(spaces).
    LOOP AT spaces INTO DATA(changeable).
      emit( kind = 'NS' text = |{ changeable-namespace }| ).
    ENDLOOP.
    emit_rc( 0 ).
  ENDMETHOD.

