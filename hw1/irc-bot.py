#!/usr/bin/env python3
import socket
import re
from time import sleep

# cleaned from https://gist.github.com/datagrok/380449c30fd0c5cf2f30
IRC_REGEX = re.compile('(?::(([^@!\ ]*)(?:(?:!([^@]*))?@([^\ ]*))?)\ )?([^\ ]+)((?:\ [^:\ ][^\ ]*){0,14})(?:\ :?(.*))?')
IRC_SERVER_INFO = ('chat.freenode.net', 6667)
IRC_USER_ID = 'owo1'
IRC_REALNAME = 'Real OwO'
IRC_CHANNEL_NAME = None

with open('config') as conf:
    s = conf.read()
    mat = re.match(r'CHAN=\'(.+?)\'', s)
    if mat is None:
        raise 'config file is malformed'
    else:
        ch = mat.group(1)
        print('Will join channel {}'.format(ch))
        IRC_CHANNEL_NAME = ch.lower()


def parse(msg):
    mat = IRC_REGEX.match(msg)
    if mat is None: return None
    grp = mat.groups()
    ret = {
        # 'raw':      msg,
        'prefix':   grp[0], # complete prefix
        'nick':     grp[1], # or servername
        'username': grp[2],
        'hostname': grp[3],
        'command':  grp[4],
        'params':   grp[5].split(' ')[1:] + ([grp[6]] if grp[6] else []),
    }

    return ret

def send(sock, msg):
    return sock.send(bytes(msg, 'utf-8'))

def recv(sock):
    return sock.recv(2048).decode('utf-8').strip()

def enumerateIP(inp):
    ret = []
    def dfs(dep, s1, s2):
        if not len(s2): return
        if dep == 3 and int(s2) < 256:
            ret.append(s1 + s2)
        for i in range(1, 4):
            if i > len(s2): break
            sL = s2[:i]
            if int(sL) < 256:
                dfs(dep + 1, s1 + sL + '.', s2[i:])
    dfs(0, '', inp)
    return ret

def convert(x):
    if x.find('0x') == 0:
        return str(int(x, 16))
    return hex(int(x))

def process(cmd, arg):
    respHead = 'PRIVMSG {} :'.format(IRC_CHANNEL_NAME)
    def resp(s):
        return send(ircsock, respHead + s + '\n')

    if cmd == 'help':
        resp('@repeat <Message>')
        resp('@convert <Number>')
        resp('@ip <String>')
    elif cmd == 'repeat':
        resp(arg)
    elif cmd == 'convert':
        resp(convert(arg))
    elif cmd == 'ip':
        if re.fullmatch(r'\d+', arg) is None:
            resp('0')
            return
        lst = enumerateIP(arg)
        resp('{}'.format(len(lst)))
        for e in lst:
            resp(e)
            sleep(.7)


ircsock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
ircsock.connect(IRC_SERVER_INFO)

send(ircsock, 'NICK {}\n'.format(IRC_USER_ID))
send(ircsock, 'USER {} 0 * :{}\n'.format(IRC_USER_ID, IRC_REALNAME))
send(ircsock, 'JOIN {} {}\n'.format(IRC_CHANNEL_NAME, 'ouo'))

intro_sent = False

with ircsock.makefile() as f:
    print('Connecting')
    while 1:
        msg = f.readline().strip('\r\n')
        if len(msg) == 0: break

        data = parse(msg)
        # print('{0[prefix]}|{0[nick]}|{0[username]}|{0[hostname]}|{0[command]}|"{0[params]}"'.format(data))

        if not intro_sent and data['command'] == '366':
            intro_sent = True
            print('Sending introduction message; ready')
            send(ircsock, 'PRIVMSG {} :Hello! I am robot.\n'.format(IRC_CHANNEL_NAME))
        elif data['command'] == 'PING':
            print('Get ping')
            send(ircsock, 'PONG {}\n'.format(IRC_CHANNEL_NAME))
        elif data['command'] == 'PRIVMSG':
            pat = re.compile('@([^ ]+)(?: (.*))?')
            tar, message = data['params']
            if tar != IRC_CHANNEL_NAME.lower():
                continue
            mat = pat.match(message)
            if mat is None: continue

            cmd, arg = mat.group(1), mat.group(2)
            print('Received command [{}]: [{}]'.format(cmd, arg))
            process(cmd, arg)
