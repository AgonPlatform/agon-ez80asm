; Verify Z180 CPU filtering and exact opcode bytes.
.cpu Z180
    IN0 B,(0)
    IN0 D,(0)
    IN0 H,(0)
    OUT0 (0),B
    OUT0 (0),D
    OUT0 (0),H
    OTIM
    OTIMR
    TST A,B
    TST A,D
    TST A,H
    TST A,(HL)
    TST A,0
    TSTIO 0
    SLP
    IN0 C,(0)
    IN0 E,(0)
    IN0 L,(0)
    IN0 A,(0)
    OUT0 (0),C
    OUT0 (0),E
    OUT0 (0),L
    OUT0 (0),A
    OTDM
    OTDMR
    TST A,C
    TST A,E
    TST A,L
    TST A,A
    MLT BC
    MLT DE
    MLT HL
    MLT SP
