    .assume adl=1
    .org 0x40000
    .relocate 0xcafe12
    jr @foo
@foo:
    nop
    .endrelocate
