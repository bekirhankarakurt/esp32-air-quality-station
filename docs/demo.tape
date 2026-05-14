# Demo recording: render with `vhs docs/demo.tape` from the repository root
# after building the host tools into ./build.
Output docs/demo.gif

Set Shell "bash"
Set FontSize 14
Set Width 1260
Set Height 600
Set Padding 16
Set Theme "Builtin Dark"
Set TypingSpeed 35ms

Hide
Type "clear" Enter
Show

Type "# Simulated station: real drivers + CRC + filters + AQI against emulated SHT31/SCD41" Enter
Type "./build/aq_simulator --speed 400 --count 6 --fault-rate 0.05" Enter
Sleep 3s

Type "# Receiver validates binary (TLMY + CRC-32) and JSON datagrams" Enter
Type "./build/aq_receiver --count 12 &" Enter
Sleep 500ms
Type "./build/aq_simulator --format both --speed 400 --count 6 --quiet" Enter
Sleep 4s
