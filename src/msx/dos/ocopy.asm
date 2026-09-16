    ORG $0100
FILE_COMMAND EQU CMD_FILE_COPY
    jp FileStart
UsageText: db 'OCOPY source.dsk destination.dsk',13,10,0
    INCLUDE 'files.inc'
    INCLUDE 'common.inc'
    ASSERT $ < $4000
