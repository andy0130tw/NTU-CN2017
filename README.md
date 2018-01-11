# :shipit: NTU CSIE Computer Network 2017 Fall

## :thought_balloon: HW1: a simple IRC bot

An IRC bot that listens to some specific commands and respond to users.

```bash
# set the channel in `config`
python3 irc-bot.py
```

## :thought_balloon: HW2: Enhanced UDP sockets

A set of file sending programs implementing TCP GBN (Go-Back-N) and congestion control on a UDP socket.

```bash
make

# Launch receiver
#          <ip>:<port>    <output-name> <buf-size>
./receiver 127.0.0.1:6666 out           1024

# Launch agent
#       <ip>:<port>    <loss-rate>
./agent 127.0.0.1:3333 0.005

#
# edit connection info corresponding to above
# and specify the  file to send in `Makefile` ...
make run-sender

# ... or you can issue the command directly
#        <ip>:<port>    <input-file> <threshold>                
AGENT=... RECV=... \
./sender 127.0.0.1:4444 input        32

# When the transfer is done, the agent should be terminated by hand.
```
