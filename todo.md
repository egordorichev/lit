# todo

* Prevent users from using break / continue outside of loops
* fix line stuff

== test.lit ==
        | [ function test.lit ]
0000 48288 OP_CONSTANT         0 'function t'
        | [ function test.lit ][ function t ]
0002    | OP_POP
        | [ function test.lit ]
0003    | OP_GET_LOCAL        1
        | [ function test.lit ][ function t ]
0005    | OP_CALL             0
== t ==
== t ==
        | [ function test.lit ][ function t ]
0000 48288 OP_CONSTANT         0 '0.003'
        | [ function test.lit ][ function t ][ 0.003 ]
0002    | OP_RETURN
        | [ function test.lit ][ 0.003 ]
0007    | OP_PRINT
0.003
        | [ function test.lit ]
0008    | OP_NULL
        | [ function test.lit ][ null ]
0009    | OP_RETURN
