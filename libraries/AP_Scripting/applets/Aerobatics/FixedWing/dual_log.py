#!/usr/bin/env python

'''
combined two aerobatics logs to generate a new log for showing the combined
path of more than one aircraft
'''

import os
from argparse import ArgumentParser

parser = ArgumentParser(description=__doc__)

parser.add_argument("log1", metavar="LOG1")
parser.add_argument("log2", metavar="LOG2")
parser.add_argument("logout", metavar="LOG2")

args = parser.parse_args()

os.environ['MAVLINK20'] = '1'

from pymavlink import mavutil

mlog1 = mavutil.mavlink_connection(args.log1)
mlog2 = mavutil.mavlink_connection(args.log2)
output = open(args.logout, mode='wb')

types1 = ['ORGN','VEH','PARM','MSG','FMT','FMTU','MULT','MODE','EVT']
types2 = ['VEH']

m1 = None
m2 = None

veh_fmt = None

def write_VEH(m, veh_fmt):
    buf = bytearray(m.get_msgbuf())
    if veh_fmt is None:
        return
    buf[2] = veh_fmt
    output.write(buf)

while True:
    if m1 is None:
        m1 = mlog1.recv_match(type=types1)
    if m2 is None:
        m2 = mlog2.recv_match(type=types2)

    if m1 is not None and m1.get_type() != 'VEH':
        output.write(m1.get_msgbuf())
        m1 = None
        continue

    if veh_fmt is None and m1 is not None:
        veh_fmt = m1.get_msgbuf()[2]

    if m1 is None and m2 is None:
        break

    if m1 is None:
        write_VEH(m2, veh_fmt)
        m2 = None
        continue

    if m2 is None:
        output.write(m1.get_msgbuf())
        m1 = None
        continue

    t1 = m1.TSec + m1.TUSec*1.0e-6
    t2 = m2.TSec + m2.TUSec*1.0e-6
    if t1 <= t2:
        output.write(m1.get_msgbuf())
        m1 = None
    else:
        write_VEH(m2, veh_fmt)
        m2 = None
