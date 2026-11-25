# ui.html -> ui_html.h (C dizgesi olarak gömer). Kullanım: python make_ui.py
out=['/* otomatik üretildi: python make_ui.py */','static const char *UI[] = {']
for line in open('ui.html',encoding='utf-8').read().split('\n'):
    e=line.replace('\\','\\\\').replace('"','\\"').replace('?','\\?')
    out.append('"%s\\n",'%e)
out.append('NULL};')
open('ui_html.h','w',encoding='utf-8').write('\n'.join(out)+'\n')
