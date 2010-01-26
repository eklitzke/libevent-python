""" Runs all unit tests for the libevent package.   """
# Copyright (c) 2006 Andy Gross.  See LICENSE.txt for details.

import sys
import unittest

from TestEvent import *
from TestEventBase import *
from TestPackage import *

if __name__=='__main__':
    unittest.main()
