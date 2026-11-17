REPORT zagtest_text_element.

TABLES t100.
SELECT-OPTIONS param1 FOR t100-arbgb.
PARAMETERS param2 TYPE c.

WRITE: 'hello world 1'(001), AT 30 'hi there'.
WRITE 'hello world 2'(abc).
