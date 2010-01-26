import unittest
import tempfile
import sys
import os
import time
import signal
import socket
import libevent

def passThroughEventCallback(fd, events, eventObj):
    return fd, events, eventObj

def makeEvent(fd=0, events=libevent.EV_WRITE):
    return libevent.createEvent(fd, events, passThroughEventCallback)	

class EventConstructionTests(unittest.TestCase):
    def testValidConstructionWithIntegerFd(self):
        event = makeEvent()

    def testEventsGetDefaultEventBase(self):
        event = makeEvent()
        self.assertEqual(event.eventBase, libevent.DefaultEventBase)
        
    def testSettingCustomEventBase(self):
        event = makeEvent()
        newEventBase = libevent.EventBase()
        event.setEventBase(newEventBase)
        self.assertEqual(event.eventBase, newEventBase)
        
    def testInvalidConstructionNonCallableCallback(self):
        self.assertRaises(libevent.EventError, libevent.Event, sys.stdout, 
                          libevent.EV_WRITE, "i'm not a callable, thats fer shure")
        
    def testValidObjectStructure(self):
        event = makeEvent(sys.stdout)
        self.assertEqual(event.fileno(), sys.stdout.fileno())
        self.assertEqual(event.callback, passThroughEventCallback)
        self.assertEqual(event.events & libevent.EV_WRITE, libevent.EV_WRITE)
        self.assertEqual(event.numCalls, 0)

    def testValidConstructionWithFileLikeObject(self):
        fp = tempfile.TemporaryFile()
        event = libevent.Event(fp, libevent.EV_WRITE, passThroughEventCallback)

    def testCreateTimer(self):
        timer = libevent.createTimer(passThroughEventCallback)

    def testTimerFlags(self):
        timer = libevent.createTimer(passThroughEventCallback)
        timer.addToLoop(1)
        self.assertEqual(timer.pending() & libevent.EV_TIMEOUT, True)
        timer.removeFromLoop()
        self.assertEqual(timer.pending() & libevent.EV_TIMEOUT, False)


class EventPriorityTests(unittest.TestCase):
    def testSettingPriority(self):
        e = makeEvent()
        e.setPriority(2)
        self.assertEqual(e.priority, 2)
        
    def testDefaultPriorityIsMiddle(self):
        e = makeEvent()
        self.assertEqual(e.priority, 1)

    def testSettingCustomPriorityCount(self):
        eventBase = libevent.EventBase(numPriorities=420)
        e = eventBase.createEvent(fd=0, events=libevent.EV_READ, callback=passThroughEventCallback)
        self.assertEqual(e.priority, 210)
        
    def testSettingPriorityAfterLoopAdd(self):
        e = makeEvent()
        e.addToLoop()
        e.setPriority(1)
        
class EventLoopSimpleTests(unittest.TestCase):
    def testSimpleSocketCallback(self):
        def serverCallback(fd, events, eventObj):
            s = socket.fromfd(fd, socket.AF_INET, socket.SOCK_STREAM)
            client, addr = s.accept()
            client.send("foo")
            eventObj.removeFromLoop()
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.setblocking(False)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind(("127.0.0.1", 50505))
        s.listen(5)
        serverEvent = libevent.createEvent(fd=s, events=libevent.EV_READ, callback=serverCallback)
        serverEvent.addToLoop()
        c = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        c.setblocking(False)
        c.connect_ex(("127.0.0.1", 50505))

        libevent.dispatch()

        c.setblocking(True)
        self.assertEqual(c.recv(3), "foo")
        c.close()
        s.close()
        
    def testSimpleTimerCallback(self):
        t = int(time.time())
        cb = lambda fd, events, obj: self.assertEqual(int(time.time())-t, 2)
        timer = libevent.createTimer(cb)
        timer.addToLoop(timeout=2)
        libevent.loop(libevent.EVLOOP_ONCE)

    def testLoopExit(self):
        cb = lambda fd, events, obj: libevent.loopExit(0)
        timer = libevent.createTimer(cb)
        timer.addToLoop(timeout=2)
        libevent.dispatch()
        
    def testSignalHandler(self):
        signalHandlerCallback = lambda signum, events, obj: obj.removeFromLoop()
        signalHandler = libevent.createSignalHandler(signal.SIGUSR1, signalHandlerCallback)
        signalHandler.addToLoop()
        signalSenderCallback = lambda fd, events, obj: os.kill(os.getpid(), signal.SIGUSR1)
        timer = libevent.createTimer(signalSenderCallback)
        timer.addToLoop(1)
        libevent.dispatch()
        # if we get here, it worked - suboptimal way to test this

if __name__=='__main__':
    unittest.main()
