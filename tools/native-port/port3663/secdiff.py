import portmap as p
for o, n in zip(p.OLD.secs, p.NEW.secs):
    print(o[0], hex(o[1]), hex(o[2]), '|', n[0], hex(n[1]), hex(n[2]))
for o, n in zip(p.OLD.code, p.NEW.code):
    a = p.OLD.d[o[3]:o[3]+min(o[2],o[4])]; b = p.NEW.d[n[3]:n[3]+min(n[2],n[4])]
    m = min(len(a), len(b))
    import numpy as np
    x = np.frombuffer(a[:m], np.uint8) != np.frombuffer(b[:m], np.uint8)
    print(o[0], 'diff bytes', int(x.sum()), 'of', m, '%.3f%%' % (100*x.sum()/m))
