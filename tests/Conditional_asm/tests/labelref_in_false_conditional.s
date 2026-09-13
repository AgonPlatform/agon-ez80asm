; Only test if calling/referencing a label from inside a FALSE conditional segment doesn't error
feature_x: .equ 0
feature_y: .equ 1

.if feature_x
  ; shouldn't emit any lines, but shouldn't error out on anyeither
  call does_not_exist
  ld    hl, does_not_exist
  ld    hl, (does_not_exist)
  jr    does_not_exist
  jp    does_not_exist
.endif

.if feature_y
  ld    a,0; give it something to do at least
.else
  ; shouldn't emit any lines, but shouldn't error out on any either
  call does_not_exist
  ld    hl, does_not_exist
  ld    hl, (does_not_exist)
  jr    does_not_exist
  jp    does_not_exist
.endif

