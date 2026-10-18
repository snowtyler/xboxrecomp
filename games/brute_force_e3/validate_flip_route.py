"""Validate flip-indexed input and completed-frame receipts, not scene equality."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct


def specification(script, captures):
    steps=[]
    for item in script.split(','):
        match=re.fullmatch(r'f(\d+):([A-Za-z0-9+\-]+):(\d+)',item)
        if not match:
            raise ValueError('Expected f<flip>:<buttons>:<hold flips>')
        at,hold=int(match[1]),int(match[3])
        if not 0 <= at < at+hold <= 0xFFFFFFFF:
            raise ValueError('Flip holds must be positive without 32-bit wrap')
        steps.append(dict(at=at,buttons=match[2],hold=hold))
    frames=set()
    for item in captures.split(','):
        match=re.fullmatch(r'(\d+)(?:-(\d+))?',item)
        if not match:
            raise ValueError('Expected N or N-M capture ranges')
        low=int(match[1]);high=int(match[2] or match[1])
        if not 1 <= low <= high <= 0xFFFFFFFF or high-low+1 > 1000:
            raise ValueError('Capture ranges require positive ordinals, at most 1000 frames')
        frames.update(range(low,high+1))
        if len(frames)>1000:
            raise ValueError('At most 1000 captured frames per comparison run')
    return steps,sorted(frames)


def summarize(prefix):
    def side(suffix):return prefix.with_name(prefix.name+suffix)
    config=json.loads(side('-process.json').read_text())['configuration']
    steps,frames=specification(config.get('RECOMP_PAD_SCRIPT',''),config.get('RECOMP_FB_DUMP_GUEST_FLIPS',''))
    log=side('.stderr.log').read_text(errors='replace')
    observed={int(i):int(flip) for _,i,flip in re.findall(
        r'PAD: t=(\d+) flips step (\d+)[^\n]*flip=(\d+)',log)}
    receipts={int(x) for x in re.findall(r'\[FRAME CAPTURE\] flip=(\d+) ',log)}
    errors=[];captures=[]
    if config.get('RECOMP_PAD_SCRIPT_EXCLUSIVE')!='1':errors.append('Exclusive scripted input not enabled')
    for i,step in enumerate(steps):
        if observed.get(i)!=step['at']:errors.append(f'Step {i} not observed at its requested flip')
    if 'PAD: missed step' in log or 'PAD: unknown button' in log or '[FRAME CAPTURE ERROR]' in log:
        errors.append('Input or capture error logged')
    for flip in frames:
        path=side(f'-frame-guest-flip-{flip:010}.bmp')
        if flip not in receipts or not path.exists():
            errors.append(f'Missing completed flip {flip}');continue
        data=path.read_bytes()
        if len(data)<54 or data[:2]!=b'BM':
            errors.append(f'Invalid bitmap at flip {flip}');continue
        width,height=struct.unpack_from('<ii',data,18)
        expected=54+((width*3+3)&~3)*height
        if width<=0 or height<=0 or len(data)!=expected:
            errors.append(f'Incomplete bitmap at flip {flip}');continue
        captures.append(dict(flip=flip,path=str(path),sha256=hashlib.sha256(data).hexdigest()))
    return dict(flip_route_checks_passed=not errors,errors=errors,steps=steps,
        observed_step_flips=observed,captures=captures,
        limits='Equal completed-flip ordinals do not fix wall-clock simulation, RNG or scene state; compare rendered scene/camera/HUD as well.')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('prefix',nargs='?',type=Path)
    parser.add_argument('--script');parser.add_argument('--captures')
    parser.add_argument('--validate-only',action='store_true')
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    if args.validate_only:
        specification(args.script or '',args.captures or '')
    else:
        if not args.prefix:parser.error('prefix required')
        result=summarize(args.prefix);text=json.dumps(result,indent=2)+'\n'
        if args.output:args.output.write_text(text)
        else:print(text)
        raise SystemExit(0 if result['flip_route_checks_passed'] else 1)
