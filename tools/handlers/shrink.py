import re,os,sys
src,dst,nh=sys.argv[1],sys.argv[2],sys.argv[3]
sdk={}; ns=None
for l in open(nh,encoding='utf-8',errors='replace'):
    m=re.match(r'\s*namespace (\w+)',l)
    if m: ns=m.group(1)
    m=re.search(r'NATIVE_DECL\s+([\w\*\s]+?)\s+(\w+)\(([^)]*)\)\s*\{.*?invoke<[^>]*>\((0x[0-9A-Fa-f]+)',l)
    if m:
        h=int(m.group(4),16)
        if h in sdk and (m.group(2).startswith("_0x") or not sdk[h].split("::")[1].startswith("_0x")): continue
        sdk[h]="%s::%s(%s) -> %s"%(ns,m.group(2),m.group(3),m.group(1).strip())
def nat(m):
    h=int(m.group(2),16)
    return "NATIVE "+sdk.get(h,"%s [not in SDK, hash %s]"%(m.group(1),m.group(2)))
for fn in os.listdir(src):
    L=open(os.path.join(src,fn),encoding='utf-8').read().split('\n'); out=[]
    for i,l in enumerate(L):
        if re.match(r'^\s+[\w\s\*:<>,]+?\b\w+(\[\d+\])*; // ', l) or re.match(r'^\s+_BYTE \w+\[\d+\]; //',l): continue
        if re.match(r'^\s+(v\d+|dwProcessId) = [\w\s\(\)\*]+;$', l) and i+1<len(L) and 'nativePush' in L[i+1]: continue
        if l.strip()=='' and out and out[-1].strip()=='': continue
        l=re.sub(r'nativeInit\(\w+\);\s+// ([\w:]+)\s+(0x[0-9A-F]+)',nat,l)
        out.append(l)
    open(os.path.join(dst,fn),'w',encoding='utf-8').write('\n'.join(out))
print(len(sdk))
