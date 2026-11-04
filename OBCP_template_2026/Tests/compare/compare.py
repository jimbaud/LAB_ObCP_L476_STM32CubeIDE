#!/usr/bin/env python3
"""
Comparaison des journaux des versions 2024 et 2026 (voir scenario.c)
====================================================================

    python3 compare.py journal_2024.txt journal_2026.txt [rapport.md]

Partie « commun » : chaque étape doit donner exactement les mêmes envois au
PC (USB) et au téléphone (BLE), et les mêmes rapports cycliques PWM, une fois
retirées les différences voulues :
  - valeurs d'accélération masquées (#.##) : la version 2024 utilisait une
    sensibilité fausse de 4 % ;
  - faute de frappe de l'aide corrigée (« b50 for red » -> « b50 for blue ») ;
  - notifications BLE vides de la version 2024 ignorées (elle en envoyait
    après « start timer », « #rst »... et à la fin d'un message de 20 octets
    exactement).
Partie « differences » : les deux réponses sont affichées côte à côte.

Code de retour : 0 si la partie commune est identique, 1 sinon.
"""
import re
import sys

FLOAT = re.compile(r'-?\d+\.\d\d')
NOTIF = re.compile(r'\[((?:[^\]\\]|\\.)*)\]')


def parse(path):
    """Rend {section: [(étape, {'USB':..., 'BLE':[...], 'PWM':...})]}"""
    sections, cur, step = {}, None, None
    for line in open(path, encoding='utf-8'):
        line = line.rstrip('\n')
        if line.startswith('# section '):
            cur = line[len('# section '):]
            sections[cur] = []
        elif line.startswith('== '):
            step = {'name': line[3:], 'USB': '', 'BLE': [], 'PWM': ''}
            sections[cur].append(step)
        elif line.startswith('USB: ') or line == 'USB:':
            step['USB'] = line[5:]
        elif line.startswith('BLE:'):
            step['BLE'] = NOTIF.findall(line[4:])
        elif line.startswith('PWM: '):
            step['PWM'] = line[5:]
    return sections


def unescape_len(s):
    """Longueur réelle d'un texte échappé (\\n, \\r, \\e, \\\\, \\xHH)."""
    return len(re.sub(r'\\x[0-9A-F]{2}|\\.', '.', s))


def chunk(text, size=20):
    """Découpe un texte échappé en morceaux de size caractères réels."""
    tokens = re.findall(r'\\x[0-9A-F]{2}|\\.|.', text)
    return [''.join(tokens[i:i + size]) for i in range(0, len(tokens), size)]


def normalize(step, old):
    usb = FLOAT.sub('#.##', step['USB'])
    ble = [n for n in step['BLE'] if n != '']
    if old:
        usb = usb.replace('b50 for red\\n\\r', 'b50 for blue\\n\\r')
        joined = ''.join(ble)
        if 'b50 for red' in joined:
            ble = chunk(joined.replace('b50 for red\\n\\r', 'b50 for blue\\n\\r'))
    ble = [FLOAT.sub('#.##', n) for n in ble]
    return usb, ble, step['PWM']


def main():
    old, new = parse(sys.argv[1]), parse(sys.argv[2])
    report = []
    errors = 0
    common_old, common_new = old.get('commun', []), new.get('commun', [])
    if [s['name'] for s in common_old] != [s['name'] for s in common_new]:
        print('Les deux journaux n\'ont pas les mêmes étapes')
        return 1
    for so, sn in zip(common_old, common_new):
        a, b = normalize(so, True), normalize(sn, False)
        if a != b:
            errors += 1
            print('DIFFERENCE : ' + so['name'])
            for label, x, y in zip(('USB', 'BLE', 'PWM'), a, b):
                if x != y:
                    print('  %s 2024 : %s' % (label, x))
                    print('  %s 2026 : %s' % (label, y))
    n = len(common_old)
    print('Partie commune : %d étapes, %d identiques, %d différentes' % (n, n - errors, errors))

    report.append('| Étape | Version 2024 | Version 2026 |')
    report.append('|---|---|---|')
    for so, sn in zip(old.get('differences', []), new.get('differences', [])):
        def cell(s):
            # une notification BLE par bloc `...` ; (vide) = notification de 0 octet
            parts = []
            if s['USB']:
                parts.append('USB : `%s`' % s['USB'])
            if s['BLE']:
                parts.append('BLE : ' + ' + '.join('`%s`' % x if x else '(vide)'
                                                   for x in s['BLE']))
            parts.append('PWM : %s' % s['PWM'])
            return '<br>'.join(parts)
        report.append('| %s | %s | %s |' % (so['name'], cell(so), cell(sn)))
    print('\nCas qui révélaient un défaut de la version 2024 :')
    print('\n'.join(report))
    if len(sys.argv) > 3:
        with open(sys.argv[3], 'w', encoding='utf-8') as f:
            f.write('\n'.join(report) + '\n')
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())
