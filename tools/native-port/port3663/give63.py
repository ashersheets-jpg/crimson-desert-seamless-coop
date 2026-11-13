"""Give on 3663: the three discard functions' heads and the client discard's body (python give63.py [n])."""
import sys
import portmap as p
import look

n = int(sys.argv[1]) if len(sys.argv) > 1 else 14
for name, a in (('kDiscardBag', 0xF31570), ('kDiscardList', 0xF318E0), ('kClientDiscard', 0xAFEE20), ('clear_wait fn', 0xF54850),
                ('slot_item fn', 0x23687F0)):
    print(name, hex(a), p.NEW.rd(a, 16).hex(' '))
    print(look.dis(p.NEW, a, n if name == 'kClientDiscard' else 6))
