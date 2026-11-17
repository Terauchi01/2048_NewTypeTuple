import ctypes,os,signal

# Linux prctl; invoked in the forked child before exec.  The taskset child then
# execs the learner without dropping the parent-death signal.
def on_parent_death(parent_pid):
 def install():
  libc=ctypes.CDLL(None,use_errno=True)
  if libc.prctl(1,signal.SIGTERM,0,0,0)!=0:os._exit(125)  # PR_SET_PDEATHSIG
  if os.getppid()!=parent_pid:os.kill(os.getpid(),signal.SIGTERM)
 return install
