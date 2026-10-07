import sys,re,os
NOISE=re.compile(r'Options"|sub_180212BA0|sub_180212D60|std::ios|ofstream|sub_180223D30|sub_1801EA390|sub_1801EDD30|dword_180426E4C|sub_1801E6440\(|filesystem')
DROP=re.compile(r'^\s+(LABEL_|goto|else$|if \( !v\d+ \)$|.*sub_18025CEB0|.*> 0xF \)|.*< 0x1000 \)|.*> 0x1F \)|.*sub_18004BF70|.*\+ 80\) = 1;|.*\+ 72\) = v|if \( !\*\(_BYTE \*\)\(v\d+ \+ 80\) \))')
seen=set()
for fn in sys.argv[2:]:
    print("=========== "+fn)
    blocks=[]; cur=[]
    for l in open(os.path.join(sys.argv[1],"%s.c"%fn),encoding='utf-8'):
        l=l.rstrip('\n')
        if l.startswith('#####') or l.startswith('  // ---- '):
            blocks.append(cur); cur=[l]
        elif not l.startswith('  (see'):
            cur.append(l)
    blocks.append(cur)
    for b in blocks:
        if not b: continue
        if b[0].startswith('  // ---- '):
            body='\n'.join(b[1:])
            if NOISE.search(body) or b[0] in seen: continue
            seen.add(b[0])
        for l in b:
            if not DROP.match(l): print(l)
