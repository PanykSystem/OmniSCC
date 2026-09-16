    ORG $0100
FILE_COMMAND EQU CMD_FILE_DELETE
    jp FileStart
UsageText: db 'ODEL path.dsk',13,10,0
    INCLUDE 'files.inc'
    INCLUDE 'common.inc'
    ASSERT $ < $4000
