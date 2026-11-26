# ui.html -> ui_html.h  ve  selftest.txt -> mp_selftest.h (C dizgesi olarak gömer). Kullanım: python make_ui.py
def conv(src,dst,name,head):
    out=[head,'static const char *%s[] = {'%name]
    for line in open(src,encoding='utf-8').read().split('\n'):
        e=line.replace('\\','\\\\').replace('"','\\"').replace('?','\\?')
        out.append('"%s\\n",'%e)
    out.append('NULL};')
    open(dst,'w',encoding='utf-8').write('\n'.join(out)+'\n')
conv('ui.html','ui_html.h','UI','/* otomatik üretildi: python make_ui.py */')
conv('selftest.txt','mp_selftest.h','SELFTEST_LINES','/* otomatik üretildi: python make_ui.py */')
