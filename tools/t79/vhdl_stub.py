import re,sys
def stub(path,ent,params=''):
    src=open(path).read()
    m=re.search(r'entity\s+%s\s+is(.*?)end\s+(entity|%s)'%(ent,ent),src,re.S|re.I)
    body=re.sub(r'--[^\n]*','',m.group(1))
    pm=re.search(r'port\s*\((.*)\)\s*;',body,re.S|re.I)
    gm=re.search(r'generic\s*\((.*?)\)\s*;\s*port',body,re.S|re.I)
    gens=[]
    if gm:
        for d in gm.group(1).split(';'):
            n=d.split(':')[0].strip()
            for x in n.split(','):
                if x.strip(): gens.append(x.strip())
    ports=[]
    for decl in pm.group(1).split(';'):
        d=decl.strip()
        if not d: continue
        mm=re.match(r'([\w\s,]+):\s*(in|out|inout)\s+(.*)',d,re.S|re.I)
        typ=mm.group(3); w=''
        r=re.search(r'\((\d+)\s+downto\s+(\d+)\)',typ)
        if r: w='[%s:%s] '%(r.group(1),r.group(2))
        dr={'in':'input','out':'output','inout':'inout'}[mm.group(2).lower()]
        for n in mm.group(1).split(','): ports.append(f'  {dr} {w}{n.strip()}')
    g=('#('+', '.join(f'parameter {x}=0' for x in gens)+') ') if gens else ''
    return f'(* blackbox *) module {ent} {g}(\n'+',\n'.join(ports)+');\nendmodule\n'
open(sys.argv[3],'w').write(stub(sys.argv[1],sys.argv[2]))
