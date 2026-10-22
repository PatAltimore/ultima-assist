print "---- wait time"
tasks: []
loop [t 1 3] [
    append tasks fibre t {{
        id: read thread-port
        wait mul 0.3 id
        print id
        id
    }}
]
while [not empty? tasks] [
    if wp: wait tasks [
        print ['task read wp]
        remove find tasks wp
    ]
]
