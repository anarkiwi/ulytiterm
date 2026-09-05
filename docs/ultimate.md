# Ultimate command interface

Implemented from the vendor documentation: *Ultimate-II Command Interface*
(Gideon Zweijtzer) for the transport, and the *Ultimate Networking Command
Technical Reference* (Scott Hutter) for the network target.

## Registers

| address | write | read |
| --- | --- | --- |
| $df1c | control | status |
| $df1d | command data | identification ($c9) |
| $df1e | | response data |
| $df1f | | status data |

Control bits: PUSH_CMD $01, DATA_ACC $02, ABORT $04, CLR_ERR $08.
Status bits: CMD_BUSY $01, DATA_ACC $02, ABORT_P $04, ERROR $08, STATE $30,
STAT_AV $40, DATA_AV $80. The state field is idle, command busy, data last,
data more.

## Sequence

1. Wait for the idle state.
2. Write the command bytes to $df1d, starting with the target ($03 for the
   network), then set PUSH_CMD.
3. Wait for a data state, then read $df1e while DATA_AV and $df1f while
   STAT_AV.
4. Set DATA_ACC. If the state was "data more", another block follows, so go
   back to step 3.

The status queue holds an ASCII result, `00` prefixed on success. Reads and
writes are bounded by a spin timeout that aborts the command, so a wedged or
absent cartridge cannot hang the terminal.

## Network commands

| byte | command |
| --- | --- |
| $05 | get IP configuration |
| $07 | TCP connect (port low, port high, host, NUL) |
| $09 | close socket |
| $10 | read socket (socket, length low, length high) |
| $11 | write socket (socket, data) |

A read replies with a 16 bit length followed by the payload. $ffff means
nothing is pending; zero means the peer closed the connection. Command length
delimits a write, so writes are binary safe.

The REU registers at $df00-$df0a sit in the same page and are unaffected: the
command interface only masks the last four register mirrors.
