from pathlib import Path

def memavail():
 for line in Path('/proc/meminfo').read_text().splitlines():
  if line.startswith('MemAvailable:'):return int(line.split()[1])*1024
 raise RuntimeError('MemAvailable unavailable')
def cgroup_v2_headroom(cgroup_text,mountinfo_text,read):
 # Read the *running process's* cgroup on each check, not an earlier SSH
 # session's transient scope. Require mountinfo root /; a cgroup namespace may
 # still hide outer ancestors, which must be checked by the operator.
 v2=[x.split(':',2)[2] for x in cgroup_text.splitlines() if x.startswith('0::')]
 if len(v2)!=1:raise RuntimeError('cannot identify unique cgroup v2 membership')
 root=Path('/sys/fs/cgroup');mounts=[]
 for line in mountinfo_text.splitlines():
  left,sep,right=line.partition(' - ')
  if not sep:continue
  fields=left.split();kind=right.split()
  if len(fields)>=5 and kind and kind[0]=='cgroup2' and fields[4]==str(root):mounts.append(fields[3])
 if mounts!=['/']:raise RuntimeError('cgroup v2 root mount missing or parent limits hidden')
 if not v2[0].startswith('/'):raise RuntimeError('invalid cgroup membership')
 rel=Path(v2[0].lstrip('/'))
 if '..' in rel.parts:raise RuntimeError('invalid cgroup path')
 current=root/rel;result=None
 while True:
  def optional(path):
   try:return read(path).strip()
   except FileNotFoundError:return None
  cap=optional(current/'memory.max');used=optional(current/'memory.current')
  if (cap is None)!=(used is None):raise RuntimeError('incomplete cgroup memory status')
  if current!=root:
   if root not in current.parents:raise RuntimeError('invalid cgroup ancestry')
   # A child has memory control iff the parent enables it in subtree_control.
   try:enabled='memory' in read(current.parent/'cgroup.subtree_control').split()
   except OSError as e:raise RuntimeError('parent cgroup controller status inaccessible') from e
   if enabled!=(cap is not None):raise RuntimeError('cgroup memory controller/status mismatch')
  # The visible cgroup-v2 root normally has no memory.max/current. A cgroup
  # namespace may hide outer ancestor limits. Non-root missing files are permitted only when the parent
  # has memory controller disabled; higher ancestor limits remain in force.
  if cap is not None:
   try:value=int(used)
   except (ValueError,TypeError) as e:raise RuntimeError('invalid cgroup current usage') from e
   if value<0:raise RuntimeError('negative cgroup usage')
   if cap!='max':
    try:maximum=int(cap)
    except ValueError as e:raise RuntimeError('invalid cgroup memory limit') from e
    if maximum<0:raise RuntimeError('negative cgroup limit')
    headroom=max(0,maximum-value)
    result=min(result,headroom) if result is not None else headroom
  if current==root:break
  current=current.parent
 return result
def cgroup_headroom():
 return cgroup_v2_headroom(Path('/proc/self/cgroup').read_text(),Path('/proc/self/mountinfo').read_text(),lambda p:p.read_text())
def available():
 a=memavail();c=cgroup_headroom()
 return min(a,c) if c is not None else a
