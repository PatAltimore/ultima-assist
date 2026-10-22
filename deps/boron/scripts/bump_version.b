#!/usr/bin/boron -s
; Bump Version v1.3.0

usage: {{
Usage: bump_version [OPTIONS]

Options:
  -f <file>     Use version specification file.  (default: dist/bump_ver.b)
  -g            Perform Git commit and tag after update.
  -h            Print this help and quit.
  -r            Revert to old version; reverse mapping of old to new.
  -v <version>  Write version into specification file.
}}

spec-file: %dist/bump_ver.b
assign-vers: none
finish: none
git-tag: false
vorder: [old new]

forall args [
    switch args/1 [
        "-f" [spec-file: to-file second ++ args]
        "-g" [git-tag: true]
        "-h" [print usage quit]
        "-r" [swap vorder]
        "-v" [assign-vers: construct second ++ args ['.' ',']]
    ]
]

if assign-vers [
    coord-ch: charset "0-9,"
    ifn parse assign-vers [some coord-ch] [
        error "Invalid version coord!"
    ]

    ; Set old to new, then assign new.
    spec: read/text spec-file
    a: b: none
    parse spec [some[
        "old: " old: some coord-ch a: '^/'
      | "new: " new: some coord-ch b: '^/'
      | thru '^/'
    ]]
    ifn all [a b] [
        error "Missing old and/or new variables!"
    ]
    spec: rejoin pick [
        [slice spec old  slice new b  slice a new  assign-vers  b]
        [slice spec new  assign-vers  slice b old  slice new b  a]
    ] lt? index? old index? new
    ; probe spec quit
    write spec-file spec
]

do spec-file


mrule: func [version coord!] [
    replace: reduce [
        str: mold version       ; $c  Coordinate version "1,2,3"
        construct str [',' '.'] ; $v  Program version    "1.2.3"
        version/1               ; $m  Major version      "1"
        version/2               ; $i  Minor version      "2"
        version/3               ; $r  Revision           "3"
    ]
    blk: make block! 10
    foreach it "cvmir" [
        append append blk join '$' it first ++ replace
    ]
    blk
]


old-rules: mrule get vorder/1
new-rules: mrule get vorder/2

crule: func [spec] [
    blk: make block! 4
    foreach it spec [
        append blk construct it old-rules
        append blk construct it new-rules
    ]
    blk
]

foreach [f mod] files [
   ;probe crule mod
    write f construct read/text f crule mod
]
do finish

if git-tag [
    vers: construct mold new [',' '.']
    rc: execute construct {git commit -a -m "Bump version to #."} ['#' vers]
    if zero? rc [
        execute join "git tag v" vers
    ]
]
