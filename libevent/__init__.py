# Copyright (c) 2006  Andy Gross <andy@andygross.org>
# Copyright (c) 2006  Nick Mathewson
# See LICENSE.txt for details.
from event import *

def createEvent(fd, events, callback):
  return DefaultEventBase.createEvent(fd, events, callback)

def createTimer(callback):
  return DefaultEventBase.createTimer(callback)

def createSignalHandler(signum, callback):
  return DefaultEventBase.createSignalHandler(signum, callback)

def loop(flags=0):
  return DefaultEventBase.loop(flags)
  
def loopExit(seconds):
  return DefaultEventBase.loopExit(seconds)
  
def dispatch():
  return DefaultEventBase.dispatch()
