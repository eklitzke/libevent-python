#!/usr/bin/python
# Copyright (c) 2006  Andy Gross <andy@andygross.org>
# Copyright (c) 2006  Nick Mathewson
# See LICENSE.txt for details.

import os, sys, ez_setup
ez_setup.use_setuptools()

from setuptools import setup, Extension, find_packages

extensions = [
    Extension("libevent.event",
        ["libevent/eventmodule.c"],
        include_dirs=["/usr/local/include"],
        library_dirs=["/usr/local/lib"],
        libraries=["event"]),
]

setup(
    name="libevent-python",
    version="0.1a8",
    description="A CPython extension module wrapping the libevent library",
    author="Andy Gross",
    author_email="andy@andygross.org",
    url="http://python-hpio.net/trac/wiki/LibEventPython",
    license="BSD",
    packages=find_packages(),
    package_data={'': ['*.txt', 'ez_setup.py', 'examples/*']},
    ext_modules = extensions,
    zip_safe = False,
    test_suite = "libevent.tests.TestAll",
    classifiers = [f.strip() for f in """
    Development Status :: 3 - Alpha
    Intended Audience :: Developers
    License :: OSI Approved :: BSD License
    Operating System :: OS Independent
    Programming Language :: Python
    Topic :: Software Development :: Libraries :: Python Modules
    Topic :: System :: Networking
    Topic :: Internet""".splitlines() if f.strip()],    
)

