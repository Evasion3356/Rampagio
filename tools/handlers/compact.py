import re,os,sys,struct
src,dst=sys.argv[1],sys.argv[2]
def fmt(a):
    a=a.strip()
    m=re.fullmatch(r'0x([0-9A-F]{8})u?',a)
    if m:
        v=int(m.group(1),16)
        f=struct.unpack('<f',struct.pack('<I',v))[0]
        if 0.0001<abs(f)<100000 and (v>>23)&0xff not in (0,255): return '%gf'%f
        return a
    m=re.fullmatch(r'\(unsigned __int64\)("[^"]*")',a)
    if m: return m.group(1)
    return a
for fn in os.listdir(src):
    L=open(os.path.join(src,fn),encoding='utf-8').read().split('\n'); out=[]; cur=None; args=[]
    for l in L:
        s=l.strip()
        if s.startswith('#####') or s.startswith('// ----') or s.startswith('(see'):
            if cur: out.append('    '+cur+'('+', '.join(args)+')'); cur=None
            out.append(l if not s.startswith('// ----') else '  '+s); continue
        m=re.match(r'NATIVE (\w+::\w+)',s)
        if m or s.startswith('nativeInit('):
            if cur: out.append('    '+cur+'('+', '.join(args)+')')
            cur=m.group(1) if m else '<same native as above?>'; args=[]; continue
        m=re.match(r'nativePush64\((.*)\);',s)
        if m and cur: args.append(fmt(m.group(1))); continue
        if 'nativeCall()' in s and cur:
            out.append('    '+cur+'('+', '.join(args)+')'); cur=None; continue
        if re.match(r'(if|else|for|while|do|switch|case|return sub_|sub_\w+\(|LABEL_|goto|\w+ = sub_|v\d+ = \*\(\w+ \*\)\(\w+ \+|\*\(.*\) = |getGlobalPtr|.*byte_|.*dword_)',s) and 'nativePush' not in s and 'sub_180220FE0' not in s and 'g_NativeHashKey' not in s:
            out.append('      '+s[:160])
    open(os.path.join(dst,fn),'w',encoding='utf-8').write('\n'.join(out))
