#!/usr/bin/env python3
"""Injecte les textes d'aide d'un fichier NOVA_<Plugin>_help_*.md dans une UI HTML.
Usage : python3 build_help.py NOVA_Compressor_help_v0.1.md nova_compressor_full_ui.html
Remplace le contenu entre /*HELP_BEGIN*/ et /*HELP_END*/ dans le HTML."""
import re,sys,json
md,html=sys.argv[1:3]
t=open(md,encoding='utf8').read()
H={}
for m in re.finditer(r'^#### (\S+)\n(.*?)(?=^#### |\Z)',t,re.S|re.M):
    f={}
    for k in('label','help_short','help_long','meta'):
        mm=re.search(r'^- %s : (.*?)(?=^- |\Z)'%k,m.group(2),re.S|re.M)
        if mm:f[k]=re.sub(r'\s*\n\s*',' ',mm.group(1)).strip()
    if 'label' not in f:
        ml=re.search(r'· label : ([^·\n]+)',m.group(2))
        if ml:f['label']=ml.group(1).strip()
    if 'help_short' not in f or 'help_long' not in f: continue
    H[m.group(1)]={'label':f.get('label',m.group(1)),'short':f['help_short'],'long':f['help_long'],'meta':f.get('meta','')}
s=open(html,encoding='utf8').read()
j=json.dumps(H,ensure_ascii=False)
s2,n=re.subn(r'/\*HELP_BEGIN\*/.*?/\*HELP_END\*/',lambda _:'/*HELP_BEGIN*/'+j+'/*HELP_END*/',s,flags=re.S)
assert n==1,'marqueurs HELP_BEGIN/END introuvables'
open(html,'w',encoding='utf8').write(s2);print(len(H),'aides injectées dans',html)
