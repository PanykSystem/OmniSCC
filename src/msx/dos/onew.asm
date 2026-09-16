    ORG $0100
FILE_COMMAND EQU CMD_FILE_NEW
    jp FileStart
UsageText: db 'ONEW path.dsk [360|720]',13,10,0
    INCLUDE 'files.inc'
    INCLUDE 'common.inc'
    ASSERT $ < $4000
