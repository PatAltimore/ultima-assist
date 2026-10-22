; bump_version for Boron project.

old: 2,0,8
new: 2,1,0
files: [
    %Makefile               ["VER=$v"]
    %dist/boron.spec        ["Version: $v"]
    %dist/control           ["Version: $v"]
    %project.b              ["%boron $c"]
    %eval/boot.b            ["version: $c"]
    %doc/UserManual.md      ["Version $v, "]
    %doc/boron.troff        ["Version $v" "boron $v"]
    %include/boron/boron.h [
        {BORON_VERSION_STR  "$v"}
        {BORON_VERSION      0x0$m0$i0$r}
    ]
    %include/boron/urlan.h [
        {UR_VERSION_STR  "$v"}
        {UR_VERSION      0x0$m0$i0$r}
    ]
]
finish: [
    print "Now run eval/mkboot, adjust manual dates, and make docs."
]
