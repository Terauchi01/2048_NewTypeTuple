#!/usr/bin/env python3
"""Run a single child in a new, isolated session; relay SIGTERM to its group."""
import os,signal,subprocess,sys
if os.getpgrp()!=os.getpid() or os.getsid(0)!=os.getpid():raise SystemExit('requires isolated session and process group')
args=sys.argv[1:];pid_file=None
if len(args)>=2 and args[0]=='--pid-file':pid_file=args[1];args=args[2:]
if len(args)<2 or args[0]!='--':raise SystemExit('usage: group_watchdog.py [--pid-file PATH] -- COMMAND ...')
def terminate(signum,frame):
 signal.signal(signal.SIGTERM,signal.SIG_IGN)
 os.killpg(os.getpgrp(),signal.SIGTERM)
 os._exit(128+signum)
signal.signal(signal.SIGTERM,terminate)
child=subprocess.Popen(args[1:])
if pid_file is not None:
 try:
  with open(pid_file,'x') as f:f.write(str(child.pid)+'\n');f.flush();os.fsync(f.fileno())
 except BaseException:
  terminate(signal.SIGTERM,None)
status=child.wait()
# A crashing child may leave a shell/hash subprocess in this dedicated group.
# Reap/terminate that group even when the main child exited without a signal.
signal.signal(signal.SIGTERM,signal.SIG_IGN)
os.killpg(os.getpgrp(),signal.SIGTERM)
sys.exit(status if status>=0 else 128-status)
