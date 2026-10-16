"""Source-vs-UE comparison with normalized shortest-arc quaternions and component-space cm."""
from pathlib import Path
import math,json
OUT=Path(r'C:\dev\output\up-pose-clip-investigation-20261016')
def norm(q):
    d=math.sqrt(sum(x*x for x in q)); return [x/d for x in q]
def qmatrix(q):
    x,y,z,w=norm(q)
    return [[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],
            [2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],
            [2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]]
def matq(m):
    tr=sum(m[i][i] for i in range(3))
    if tr>0:
        s=math.sqrt(tr+1)*2
        return norm([(m[2][1]-m[1][2])/s,(m[0][2]-m[2][0])/s,(m[1][0]-m[0][1])/s,s/4])
    i=max(range(3),key=lambda i:m[i][i]); j=(i+1)%3;k=(i+2)%3
    s=math.sqrt(1+m[i][i]-m[j][j]-m[k][k])*2
    q=[0.0]*4;q[i]=s/4;q[j]=(m[j][i]+m[i][j])/s;q[k]=(m[k][i]+m[i][k])/s;q[3]=(m[k][j]-m[j][k])/s
    return norm(q)
def mul(a,b): return [[sum(a[i][k]*b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]
def transpose(a): return [list(row) for row in zip(*a)]
def angle(q,r):
    q,r=norm(q),norm(r)
    # Chord distance is stable near zero; q and -q are equivalent.
    d=min(math.sqrt(sum((a-b)**2 for a,b in zip(q,r))),math.sqrt(sum((a+b)**2 for a,b in zip(q,r))))
    return math.degrees(4*math.asin(min(1.0,d/2)))
def distance(p,q): return math.sqrt(sum((a-b)**2 for a,b in zip(p,q)))
def compare_file(filename):
    src=json.loads((OUT/'evidence/source_poses.json').read_text())
    ue=json.loads((OUT/'evidence'/filename).read_text())
    C=[[1,0,0],[0,-1,0],[0,0,1]]
    result={'file':filename,'method':'component positions (x,-y,z)*100 cm; component rotation C * R_source_pose * inverse(R_source_rest) * C * R_UE_rest; normalized quaternions, shortest arc',
        'reference_max_cm':max(distance([m[0][3]*100,-m[1][3]*100,m[2][3]*100],ue['reference'][b]['p']) for b,m in src['rest'].items()),'variants':{}}
    def expected(name,b):
        pose=src['poses'][name][b]; rest=src['rest'][b]
        rp=qmatrix(matq(pose)); rr=qmatrix(matq(rest)); ur=qmatrix(ue['reference'][b]['q'])
        return {'p':[pose[0][3]*100,-pose[1][3]*100,pose[2][3]*100],'q':matq(mul(mul(mul(mul(C,rp),transpose(rr)),C),ur))}
    def check(name,item):
        if 'error' in item: return item
        assert set(item['samples'])=={'raw','compressed'}, name
        per={}; mx={'cm':0.0,'deg':0.0,'scale_dev':0.0,'compressed_vs_raw_cm':0.0,'compressed_vs_raw_deg':0.0,'seam_cm':0.0,'seam_deg':0.0}
        for kind,frames in item['samples'].items():
            assert len(frames)==item['keys'] and len(frames)>0,(name,kind)
            per[kind]={}
            for frame,bones in frames.items():
                assert set(bones)==set(src['rest']) and len(bones)==22,(name,kind,frame)
                per[kind][frame]={}
                for b,t in bones.items():
                    ex=expected(name,b)
                    e={'cm':distance(t['p'],ex['p']),'deg':angle(t['q'],ex['q'])}
                    per[kind][frame][b]=e
                    for metric in ('cm','deg'):
                        if e[metric]>mx[metric]: mx[metric]=e[metric];mx['worst_'+metric]=[kind,frame,b]
                    mx['scale_dev']=max(mx['scale_dev'],max(abs(s-1) for s in t['s']))
                    first=frames['0'][b]
                    mx['seam_cm']=max(mx['seam_cm'],distance(first['p'],t['p']))
                    mx['seam_deg']=max(mx['seam_deg'],angle(first['q'],t['q']))
                    if kind=='compressed':
                        raw=item['samples']['raw'][frame][b]
                        mx['compressed_vs_raw_cm']=max(mx['compressed_vs_raw_cm'],distance(raw['p'],t['p']))
                        mx['compressed_vs_raw_deg']=max(mx['compressed_vs_raw_deg'],angle(raw['q'],t['q']))
        return {'keys':item['keys'],'seconds':item['seconds'],'sample_dt':item['sample_dt'],'max':mx,'per_bone':per,'pass_source':mx['cm']<=0.001 and mx['deg']<=0.01}
    for variant,data in ue['variants'].items():
        clips={n:check(n,e) for n,e in data['clips'].items()}
        add={n:check(n,e['full_pose']) for n,e in data.get('additive',{}).items()}
        valid=[v for v in clips.values() if 'max' in v]
        result['variants'][variant]={'clips':clips,'additive':add,'summary':{'clips':len(clips),'errors':len(clips)-len(valid),
            'max_cm':max((v['max']['cm'] for v in valid),default=None),'max_deg':max((v['max']['deg'] for v in valid),default=None),
            'source_pass':sum(v['pass_source'] for v in valid),'keys':sorted(set(v['keys'] for v in valid)),'lengths':sorted(set(v['seconds'] for v in valid)),
            'additive_pass':sum(v.get('pass_source',False) for v in add.values()),'additive_count':len(add)}}
    dest=OUT/'evidence'/filename.replace('.json','_comparison.json')
    dest.write_text(json.dumps(result,indent=2))
    print('reference max cm',result['reference_max_cm'])
    print(json.dumps({k:v['summary'] for k,v in result['variants'].items()},indent=2))
if __name__=='__main__':
    import sys
    compare_file(sys.argv[1] if len(sys.argv)>1 else 'ue_imports.json')
