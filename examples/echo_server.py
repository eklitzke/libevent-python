"""
A poorly-factored but kinda-working example of an echo server.
"""

import sys
import socket
import signal
import libevent

class BaseConnection(object):
    bufferSize = 2**16
    def __init__(self, sock, addr, server):
        self.sock = sock
        self.addr = addr
        self.server = server
        self.sock.setblocking(False)
        self.buf = []
        self.readEvent = libevent.createEvent(
            self.sock,libevent.EV_READ|libevent.EV_PERSIST, self._doRead)
        self.writeEvent = libevent.createEvent(
            self.sock,libevent.EV_WRITE, self._doWrite)
        self.startReading()

    def startReading(self):
        self.readEvent.addToLoop()

    def stopReading(self):
        self.readEvent.removeFromLoop()
            
    def startWriting(self):
        self.writeEvent.addToLoop()
        
    def stopWriting(self):
        self.writeEvent.removeFromLoop()

    def _doRead(self, fd, events, eventObj):
        data = ''
        data = self.sock.recv(self.bufferSize)
        if not data:
            self.server.lostClient(self)
            self.stopReading()
            self.stopWriting()
            self.sock.close()
        else:
            self.gotData(data)
            
    def _doWrite(self, fd, events, eventObj):
        data = "".join(self.buf)
        nsent = self.sock.send(data)
        data = data[nsent:]
        if not data:
            self.stopWriting()
            self.buf = []
        else:
            self.buf = [data]
            if not self.writeEvent.pending():
                self.startWriting()
            
    def write(self, data):
        self.buf.append(data)
        self.startWriting()
        
    def gotData(self, data):
        raise NotImplementedError

class EchoConnection(BaseConnection):
    def gotData(self, data):
        self.write(data)
            
class Acceptor(object):
    def __init__(self,  addr, port, server):
        self.addr = addr
        self.port = port
        self.server = server
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.setblocking(False)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        
    def listen(self):
        self.sock.bind((self.addr, self.port))
        self.sock.listen(5)
        events = libevent.EV_READ|libevent.EV_PERSIST
        libevent.createEvent(self.sock, events, self._callback).addToLoop()
        
    def _callback(self, fd, events, eventObj):
        sock, addr = self.sock.accept()
        self.server.gotClient(sock, addr)

class EchoServer(object):
    def __init__(self, addr="127.0.0.1", port=50505):
        self.acceptor = Acceptor(addr, port, self)
        self.clients = dict()
        self.acceptor.listen()

    def gotClient(self, sock, addr):
        print "Got connection from %s:%s" % addr
        client = EchoConnection(sock, addr, self)
        self.clients[addr] = client
        
    def lostClient(self, client):
        print "Lost connection from %s:%s" % client.addr
        client = self.clients[client.addr]
        del self.clients[client.addr]
        del client
        
def handleSigInt(signum, events, obj):
    libevent.loopExit(0)
    raise KeyboardInterrupt

def main():
    libevent.createSignalHandler(signal.SIGINT, handleSigInt).addToLoop()
    echosrv = EchoServer()
    libevent.dispatch()

if __name__ == "__main__":
    sys.exit(main())
      
