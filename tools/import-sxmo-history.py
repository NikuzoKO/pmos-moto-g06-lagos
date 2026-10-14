import gi, os, sys, sqlite3, time, re, glob
from datetime import datetime, timezone
gi.require_version('EDataServer','1.2'); gi.require_version('EBook','1.2'); gi.require_version('EBookContacts','1.2')
from gi.repository import EDataServer, EBook, EBookContacts
H=os.path.expanduser('~')
what=sys.argv[1:]
if 'contacts' in what:
    reg=EDataServer.SourceRegistry.new_sync(None)
    src=reg.ref_builtin_address_book()
    cl=EBook.BookClient.connect_sync(src,30,None)
    names={}
    for l in open(H+'/.config/sxmo/contacts.tsv'):
        p=l.rstrip('\n').split('\t')
        if len(p)>=2 and p[1].strip(): names.setdefault(p[1].strip(),[]).append(p[0].strip())
    existing=cl.get_contacts_sync('(contains "x-evolution-any-field" "")',None)[1]
    have={c.get_property('full-name') for c in existing}
    n=0
    for name,nums in names.items():
        if name in have: continue
        v="BEGIN:VCARD\nVERSION:3.0\nFN:%s\nN:%s;;;;\n"%(name,name)+"".join("TEL;TYPE=CELL:%s\n"%x for x in nums)+"END:VCARD\n"
        cl.add_contact_sync(EBookContacts.Contact.new_from_vcard(v),EBookContacts.BookOperationFlags.NONE,None); n+=1
    print('contacts added',n,'of',len(names))
def ts(s): return int(datetime.strptime(s,'%Y-%m-%dT%H:%M:%S%z').timestamp())
if 'sms' in what:
    db=sqlite3.connect(H+'/.purple/chatty/db/chatty-history.db'); c=db.cursor()
    acc=c.execute("select accounts.id from accounts join users on users.id=user_id where username like 'invalid-%'").fetchone()[0]
    n=0
    for d in glob.glob(H+'/.local/share/sxmo/modem/*/sms.txt'):
        num=os.path.basename(os.path.dirname(d))
        txt=open(d).read()
        parts=re.split(r'^(Received|Sent) at (\S+):\n',txt,flags=re.M)
        c.execute("insert or ignore into users(username,type) values(?,1)",(num,))
        uid_=c.execute("select id from users where username=? and type=1",(num,)).fetchone()[0]
        c.execute("insert or ignore into threads(name,account_id,type) values(?,?,0)",(num,acc))
        th=c.execute("select id from threads where name=? and account_id=?",(num,acc)).fetchone()[0]
        c.execute("insert or ignore into thread_members(thread_id,user_id) values(?,?)",(th,uid_))
        for i in range(1,len(parts),3):
            kind,t,body=parts[i],ts(parts[i+1]),parts[i+2].strip('\n')
            if not body: continue
            r=c.execute("insert or ignore into messages(uid,thread_id,sender_id,body,body_type,direction,time,status) values(?,?,?,?,1,?,?,?)",
              ('imp-%s-%d'%(num,t),th,uid_ if kind=='Received' else None,body,1 if kind=='Received' else -1,t,2 if kind=='Received' else 3))
            n+=r.rowcount
    db.commit(); print('sms added',n)
if 'calls' in what:
    db=sqlite3.connect(H+'/.local/share/calls/records.db'); c=db.cursor()
    def iso(t): return datetime.fromtimestamp(t,timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')
    ev=[]
    for l in open(H+'/.local/share/sxmo/modem/modemlog.tsv',errors='replace').read().replace('\x00','').splitlines():
        p=l.rstrip('\n').split('\t')
        if len(p)>=3 and p[1].startswith(('call_')): ev.append((ts(p[0]),p[1],p[2]))
    cur={}; rows=[]
    for t,e,num in ev:
        if e=='call_ring': cur[num]=dict(inb=1,start=t,ans=None)
        elif e=='call_start': cur[num]=dict(inb=0,start=t,ans=None)
        elif e=='call_pickup' and num in cur: cur[num]['ans']=t
        elif e in('call_hangup','call_missed','call_discard') and num in cur:
            r=cur.pop(num); r['end']=t
            if r['inb']==0 and t>r['start']: r['ans']=r['start']
            if e!='call_hangup': r['ans']=None
            rows.append((num,r))
    n=0
    for num,r in rows:
        if c.execute("select 1 from calls where target=? and start=?",(num,iso(r['start']))).fetchone(): continue
        c.execute("insert into calls(target,inbound,start,answered,end,protocol) values(?,?,?,?,?,'tel')",
          (num,r['inb'],iso(r['start']),iso(r['ans']) if r['ans'] else None,iso(r['end'])))
        n+=1
    db.commit(); print('calls added',n,'of',len(rows))
