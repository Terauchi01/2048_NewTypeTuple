"""Attested save-before-stop; never sends group signals or SIGKILL.

A dead learner cannot be saved. Such an exit is recorded as unsaved, never as
successful recovery. Active learners wait without a forced termination timeout.
"""
import hashlib,json,os,shlex,signal,time
from pathlib import Path
from request_save_and_stop import issue,validate_request

def process_state(child):
    try:
        p=Path('/proc')/str(child['pid'])
        s=(p/'stat').read_text().rsplit(')',1)[1].split()
        if p.stat().st_uid!=os.getuid():raise RuntimeError('learner UID mismatch')
        if int(s[19])!=child['starttick']:return None
        return s[0]
    except (FileNotFoundError,ProcessLookupError):return None

def digest(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as f:
        for b in iter(lambda:f.read(1024**2),b''):h.update(b)
    return h.hexdigest()

def ensure_request(child):
    run=Path(child['run_dir']).resolve()
    c=json.loads((run/'config.json').read_text())
    for field in ('source_sha256','binary_sha256'):
        if c.get(field)!=child[field]:raise RuntimeError('config/release identity mismatch: '+field)
    request=Path(c['save_and_stop_file'])
    if request.exists():
        tokens=shlex.split(request.read_text());nonce=tokens[-1] if tokens else ''
        if not validate_request(request,run,child['pid'],child['starttick'],os.getuid(),child['source_sha256'],child['binary_sha256'],nonce):raise RuntimeError('existing save request identity mismatch')
        return dict(status='request_already_issued',request=str(request),nonce=nonce)
    return issue(run,child['pid'],child['starttick'])

def verify_saved(child,verify_model=True):
    run=Path(child['run_dir']).resolve()
    config=json.loads((run/'config.json').read_text())
    for field,key in (('process_pid','pid'),('process_starttick','starttick')):
        if key in child and config.get(field)!=child[key]:raise RuntimeError('saved run/process identity mismatch: '+field)
    if config.get('process_uid')!=os.getuid():raise RuntimeError('saved run UID mismatch')
    markers=[p for p in (run/'RUN_COMPLETE.json',run/'RUN_PAUSED.json') if p.is_file()]
    if len(markers)!=1:raise RuntimeError('missing/ambiguous terminal marker; save is not confirmed')
    marker=json.loads(markers[0].read_text())
    for field in ('source_sha256','binary_sha256'):
        if marker.get(field)!=child[field]:raise RuntimeError('terminal/release identity mismatch: '+field)
    latest=run/'checkpoints/LATEST'
    if not latest.is_file():raise RuntimeError('missing checkpoint LATEST')
    name=latest.read_text().strip()
    checkpoint=(latest.parent/name).resolve()
    if checkpoint.parent!=latest.parent.resolve() or not checkpoint.is_dir():raise RuntimeError('checkpoint LATEST escapes checkpoint directory')
    meta=json.loads((checkpoint/'checkpoint.json').read_text())
    for field in ('source_sha256','binary_sha256','actual_steps','save_ordinal'):
        if meta.get(field)!=marker.get(field):raise RuntimeError('checkpoint/terminal mismatch: '+field)
    commit=(checkpoint/'COMPLETE').read_text().split()
    if len(commit)!=3 or commit[0]!='EXPLORATION_FULL_V2':raise RuntimeError('invalid checkpoint commit')
    if digest(checkpoint/'driver.txt')!=commit[1]:raise RuntimeError('driver checksum mismatch')
    model=checkpoint/'model.bin'
    if not model.is_file() or model.stat().st_size<200:raise RuntimeError('missing/truncated checkpoint model')
    if verify_model and digest(model)!=commit[2]:raise RuntimeError('model checksum mismatch')
    return dict(status='saved_and_exited',marker=str(markers[0]),checkpoint=str(checkpoint),actual_steps=marker['actual_steps'],model_checksum_verified=verify_model)

def signal_save_stop(child):
    if not hasattr(os,'pidfd_open') or not hasattr(signal,'pidfd_send_signal'):
        raise RuntimeError('pidfd unavailable; continuing request retries without an unsafe PID signal')
    fd=os.pidfd_open(child['pid'],0)
    try:
        if process_state(child) in (None,'Z'):return False
        if digest(Path('/proc')/str(child['pid'])/'exe')!=child['binary_sha256']:
            raise RuntimeError('refuse save signal: executable identity mismatch')
        signal.pidfd_send_signal(fd,signal.SIGTERM,None,0)
        return True
    finally:os.close(fd)

def pause_children(children,*,poll_seconds=2,report=None,verify_model=True):
    """Wait for all registered learners to save and exit, retrying request IO.

    SIGTERM is a fallback sent only to a verified learner leader, whose pinned
    build implements save-on-signal. SHA/checkpoint subprocesses are not signaled.
    """
    states={};pending=list(children)
    while pending:
        remaining=[]
        for child in pending:
            key=child.get('task',str(child['pid']));row=states.setdefault(key,dict(status='save_pending',pid=child['pid'],starttick=child['starttick']))
            try:
                state=process_state(child)
                if state is None or state=='Z':
                    if child.get('never_started'):
                        row.update(status='exited_before_model_initialization')
                    else:
                        try:row.update(verify_saved(child,verify_model))
                        except (OSError,ValueError,RuntimeError) as e:row.update(status='exited_without_verified_save',error=str(e))
                    continue
                if child.get('never_started'):
                    remaining.append(child);continue
                try:
                    row['request']=ensure_request(child);row['status']='waiting_for_saved_exit';row.pop('request_error',None)
                except (OSError,ValueError,RuntimeError) as e:
                    row['request_error']=str(e);row['request_attempts']=row.get('request_attempts',0)+1
                    if row['request_attempts']>=3 and not row.get('signal_sent'):
                        if signal_save_stop(child):row.update(signal_sent=True,status='waiting_for_save_on_signal')
                remaining.append(child)
            except (OSError,ValueError,RuntimeError) as e:
                row.update(status='save_pending_identity_or_io_error',error=str(e));remaining.append(child)
        pending=remaining
        if report:
            try:report(dict(states=states,pending=len(pending),epoch=time.time()))
            except (OSError,ValueError,RuntimeError):pass
        if pending:time.sleep(poll_seconds)
    return dict(status='saved' if all(x['status']in ('saved_and_exited','exited_before_model_initialization')for x in states.values())else'unsaved_exit_detected',children=states)
