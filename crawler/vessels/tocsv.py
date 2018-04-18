import string
import sys

first = False
with open('vessels.txt', 'w', encoding='utf8') as fout:
    # write header
    with open('vessels_%s.txt' % 'a', "r", encoding='utf8') as f:
        for line in f.readlines():
            s = line.split(':')
            if len(s) == 2:
                key = s[0].strip()
                val = s[1].strip()
                if key == 'exname 9':
                    sys.stdout.write(key + '\n')
                    fout.write(key + '\n')
                    break
                else:
                    sys.stdout.write(key + '\t')
                    fout.write(key + '\t')

    for c in string.ascii_lowercase:
        with open('vessels_%s.txt' % c, "r", encoding='utf8') as f:
            for line in f.readlines():
                s = line.split(':')
                if len(s) == 2:
                    key = s[0].strip()
                    val = s[1].strip()
                    if key == 'exname 9':
                        sys.stdout.write(val + '\n')
                        fout.write(val + '\n')
                    else:
                        sys.stdout.write(val + '\t')
                        fout.write(val + '\t')
