import unittest
import libevent

__all__ = ["EventBaseTests"]

class EventBaseTests(unittest.TestCase):
    def testEventBaseValidConstructionNoArgs(self):
        eventBase = libevent.EventBase()

    def testEventBaseValidConstructionOneArg(self):
        eventBase = libevent.EventBase(3)
        
    def testEventBaseValidConstructionKwargs(self):
        eventBase = libevent.EventBase(numPriorities=3)
        
    def testEventBaseInvalidConstruction(self):
        self.assertRaises(TypeError, libevent.EventBase, stupid=1)

if __name__=='__main__':
    unittest.main()
