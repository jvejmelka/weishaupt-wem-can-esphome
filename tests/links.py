#!/usr/bin/env python3
"""Prueft relative Links in allen .md-Dateien des Repositories: Datei vorhanden und,
falls ein #Anker angegeben ist, Ueberschrift vorhanden (GitHub-Schreibweise).
Externe Links (http, https, mailto) werden nicht abgerufen."""
import os, re, sys, unicodedata

WURZEL = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LINK = re.compile(r'(?<!!)\[[^\]]*\]\(([^)\s]+)(?:\s+"[^"]*")?\)')
UEBERSCHRIFT = re.compile(r'^(#{1,6})\s+(.*?)\s*#*\s*$')


def slug(text):
    text = re.sub(r'`([^`]*)`', r'\1', text)
    text = re.sub(r'\[([^\]]*)\]\([^)]*\)', r'\1', text)
    text = text.strip().lower()
    erlaubt = []
    for c in text:
        if c.isalnum() or c in '-_ ':
            erlaubt.append(c)
        elif unicodedata.category(c).startswith(('L', 'N')):
            erlaubt.append(c)
    return ''.join(erlaubt).replace(' ', '-')


def anker(pfad):
    gesehen, ergebnis, im_code = {}, set(), False
    with open(pfad, encoding='utf-8') as f:
        for zeile in f:
            if zeile.lstrip().startswith('```'):
                im_code = not im_code
                continue
            m = None if im_code else UEBERSCHRIFT.match(zeile)
            if not m:
                continue
            s = slug(m.group(2))
            n = gesehen.get(s, 0)
            gesehen[s] = n + 1
            ergebnis.add(s if n == 0 else f'{s}-{n}')
    return ergebnis


def main():
    fehler = 0
    dateien = []
    for d, unter, namen in os.walk(WURZEL):
        unter[:] = [u for u in unter if not u.startswith('.') and u != 'node_modules']
        dateien += [os.path.join(d, n) for n in namen if n.endswith('.md')]
    cache = {}
    for pfad in sorted(dateien):
        im_code = False
        with open(pfad, encoding='utf-8') as f:
            for nr, zeile in enumerate(f, 1):
                if zeile.lstrip().startswith('```'):
                    im_code = not im_code
                if im_code:
                    continue
                for ziel in LINK.findall(zeile):
                    if re.match(r'^[a-z]+:', ziel):
                        continue
                    datei, _, frag = ziel.partition('#')
                    zpfad = os.path.normpath(os.path.join(os.path.dirname(pfad), datei)) if datei else pfad
                    rel = os.path.relpath(pfad, WURZEL)
                    if not os.path.exists(zpfad):
                        print(f'{rel}:{nr}: Ziel fehlt: {ziel}')
                        fehler += 1
                        continue
                    if frag and zpfad.endswith('.md'):
                        if zpfad not in cache:
                            cache[zpfad] = anker(zpfad)
                        if frag.lower() not in cache[zpfad]:
                            print(f'{rel}:{nr}: Anker fehlt: {ziel}')
                            fehler += 1
    print(f'{len(dateien)} Dateien geprueft, {fehler} Fehler')
    return 1 if fehler else 0


if __name__ == '__main__':
    sys.exit(main())
