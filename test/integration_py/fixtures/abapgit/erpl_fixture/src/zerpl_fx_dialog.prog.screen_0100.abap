PROCESS BEFORE OUTPUT.
  MODULE status_0100.

PROCESS AFTER INPUT.
  MODULE exit_command AT EXIT-COMMAND.
  FIELD snwd_so-so_id MODULE check_so_id.
  MODULE user_command_0100.

PROCESS ON VALUE-REQUEST.
  FIELD snwd_so-so_id MODULE f4_so_id.
