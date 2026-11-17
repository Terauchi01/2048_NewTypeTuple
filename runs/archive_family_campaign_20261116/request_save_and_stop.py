#!/usr/bin/env python3
"""Request a complete manual checkpoint and clean pause. Run on learner host.
Issuing a request is not confirmation that saving has completed.
"""
import argparse,hashlib,json,os,shlex,tempfile,uuid
from pathlib import Path

def sha(path):
    h=hashlib.sha256()
    with open(path,'rb') as f:
        for block in iter(lambda:f.read(1024**2),b''):h.update(block)
    return h.hexdigest()

def identity(pid):
    p=Path('/proc')/str(pid)
    fields=(p/'stat').read_text().rsplit(')',1)[1].split()
    if fields[0]=='Z':raise RuntimeError('learner already exited')
    return p,dict(pid=pid,starttick=int(fields[19]),uid=p.stat().st_uid)

def issue(run_dir,pid,expect_starttick=None):
    run_dir=Path(run_dir).resolve();config_path=run_dir/'config.json'
    c=json.loads(config_path.read_text());proc,record=identity(pid)
    if c.get('save_and_stop_capability')!='manual_checkpoint_stop_v1':raise RuntimeError('this binary does not support manual save-and-stop')
    if record['uid']!=os.getuid() or config_path.stat().st_uid!=os.getuid():raise RuntimeError('run/process UID mismatch')
    if record['pid']!=c.get('process_pid') or record['starttick']!=c.get('process_starttick') or record['uid']!=c.get('process_uid'):raise RuntimeError('config/process identity mismatch')
    if expect_starttick is not None and record['starttick']!=expect_starttick:raise RuntimeError('unexpected PID starttick')
    boot=Path('/proc/sys/kernel/random/boot_id').read_text().strip()
    if boot!=c.get('process_boot_id'):raise RuntimeError('boot identity mismatch')
    cmd=(proc/'cmdline').read_bytes().decode().split('\0');index=cmd.index('--output-dir');argument=Path(cmd[index+1]);actual=(argument if argument.is_absolute() else (proc/'cwd').resolve()/argument).resolve()
    if actual!=run_dir:raise RuntimeError('target process output directory mismatch')
    if sha(proc/'exe')!=c['binary_sha256']:raise RuntimeError('running binary hash mismatch')
    if (run_dir/'RUN_PAUSED.json').exists() or (run_dir/'RUN_COMPLETE.json').exists():raise RuntimeError('run has already terminated')
    request=Path(c['save_and_stop_file']);request.parent.mkdir(parents=True,exist_ok=True)
    if request.exists():raise FileExistsError('request already exists; it will not be overwritten')
    nonce=uuid.uuid4().hex
    strings=[boot,str(run_dir),c['binary_sha256'],c['source_sha256'],nonce]
    if any(any(ord(ch)<32 for ch in text)for text in strings):raise RuntimeError('control characters in request identity')
    text='EXPLORATION_SAVE_STOP_V1\n'+f"{pid} {record['starttick']} {record['uid']}\n"+'\n'.join(json.dumps(x,ensure_ascii=False)for x in strings)+'\n'
    fd,tmp=tempfile.mkstemp(prefix=request.name+'.tmp-',dir=request.parent)
    try:
        with os.fdopen(fd,'w')as f:f.write(text);f.flush();os.fsync(f.fileno())
        if identity(pid)[1]!=record:raise RuntimeError('process changed while preparing request')
        os.link(tmp,request) # Atomic publish, refuse to overwrite an old request.
        directory=os.open(request.parent,os.O_RDONLY|os.O_DIRECTORY)
        try:os.fsync(directory)
        finally:os.close(directory)
    finally:os.unlink(tmp)
    return dict(status='request_issued_not_saved_yet',run_dir=str(run_dir),request=str(request),nonce=nonce,**record)

def validate_request(path,run_dir,pid,starttick,uid,source,binary,nonce):
    tokens=shlex.split(Path(path).read_text())
    return len(tokens)==9 and tokens[4]==Path('/proc/sys/kernel/random/boot_id').read_text().strip() and tokens[0]=='EXPLORATION_SAVE_STOP_V1' and tokens[1:4]==[str(pid),str(starttick),str(uid)] and tokens[5:]==[str(Path(run_dir).resolve()),binary,source,nonce]

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--run-dir',type=Path,required=True);p.add_argument('--pid',type=int,required=True);p.add_argument('--expect-starttick',type=int)
    a=p.parse_args();print(json.dumps(issue(a.run_dir,a.pid,a.expect_starttick),indent=2))
if __name__=='__main__':main()
