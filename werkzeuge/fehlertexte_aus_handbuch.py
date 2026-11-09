#!/usr/bin/env python3
"""Originaltexte der Warn- und Fehlercodes aus der eigenen Weishaupt-Anleitung holen.

Warum es dieses Werkzeug gibt: Die Texte der Montage- und Betriebsanleitung sind urheberrechtlich
geschuetzt (Rechte bei Weishaupt). Das Repo enthaelt deshalb nur eigene Kurztexte (fehlercodes.yaml).
Wer die Originaltexte sehen will, laedt die Anleitung selbst bei Weishaupt herunter und erzeugt damit
eine lokale Datei. Diese Datei gehoert NICHT ins Repo (steht in .gitignore).

Anleitung WTC-GW 15 ... 32-B (oeffentlich bei Weishaupt):
  Deutsch:  https://www.weishaupt.de/uploads/tx_weishaupt_documents/documents/83307201.pdf
  Englisch: https://www.weishaupt.de/uploads/tx_weishaupt_documents/documents/83307202.pdf

Aufruf:
  python3 werkzeuge/fehlertexte_aus_handbuch.py 83307201.pdf [83307202.pdf ...]
  -> schreibt fehlertexte-original.json (Sprache wird aus der Tabellenkopfzeile erkannt)
  python3 werkzeuge/fehlertexte_aus_handbuch.py --header 83307201.pdf
  -> schreibt zusaetzlich components/weishaupt_can/fehlertexte_original.h; beim naechsten Bau zeigt die
     Firmware dann die Originaltexte statt der eigenen (ohne die Datei: eigene Texte).

Braucht pdftotext (Paket poppler-utils).
"""
import json
import re
import subprocess
import sys
from pathlib import Path

WURZEL = Path(__file__).resolve().parent.parent
KOEPFE = {  # Sprache: (Wort fuer Warnung in der Kopfzeile, Ursache, Behebung)
    'de': ('Warnung', 'Ursache', 'Behebung'),
    'en': ('Warning', 'Cause', 'Rectification'),
}
KOPF_FEHLER = {'de': 'Fehler', 'en': 'Fault'}
CODE = re.compile(r'^\s*([WF]) (\d+)(?: …)?\s+(.*)$')


def text_der_pdf(pdf):
    return subprocess.run(['pdftotext', '-layout', str(pdf), '-'], check=True,
                          capture_output=True, text=True).stdout


def sprache_und_kapitel(text):
    """Sprache erkennen und nur die Zeilen von Kapitel 10.2/10.3 zurueckgeben."""
    for sp, (w, u, b) in KOEPFE.items():
        kopf = re.compile(rf'^\s*(?:{w}|{KOPF_FEHLER[sp]})\s+{u}\s+{b}\s*$')
        zeilen = text.splitlines()
        if any(kopf.match(z) for z in zeilen):
            return sp, zeilen, kopf
    raise SystemExit('Keine Tabelle "Warnung/Ursache/Behebung" bzw. "Warning/Cause/Rectification" gefunden.')


def auslesen(zeilen, kopf, sprache):
    """Spalte "Ursache" je Code einsammeln. Die Spaltengrenzen kommen aus der jeweils letzten Kopfzeile."""
    ergebnis, aktuell = {}, None
    u_start = b_start = None
    in_tabelle = False
    for z in zeilen:
        if kopf.match(z):
            u_start = z.index(KOEPFE[sprache][1])
            b_start = z.index(KOEPFE[sprache][2])
            in_tabelle = True
            continue
        if not in_tabelle:
            continue
        if re.match(r'^\s*\d{8} \d/\d{4}-\d{2}', z) or re.match(r'^\s*1[01](\.\d)? ', z):
            aktuell = None  # Seitenfuss oder naechstes Kapitel
            if re.match(r'^\s*11 ', z):
                in_tabelle = False
            continue
        m = CODE.match(z)
        if m:
            typ, nr = m.group(1), int(m.group(2))
            aktuell = f'{typ} {nr}'
            # Codezeile: alles nach der Nummer bis zur Behebungsspalte
            rest = z[m.start(3):]
            ursache = rest[:max(0, b_start - 2 - m.start(3))] if len(z) > b_start else rest
            ergebnis.setdefault(aktuell, []).append([ursache.strip()])
            continue
        if aktuell and z.strip():
            stueck = z[:b_start - 2].strip() if b_start else z.strip()
            bis = re.match(r'^(\d{3,4})\b\s*(.*)$', stueck)
            if bis and z[:1].isdigit():  # Bereich "W 1501 …" + naechste Zeile "1532"
                neu = f'{aktuell}-{bis.group(1)}'
                ergebnis[neu] = ergebnis.pop(aktuell)
                aktuell = neu
                stueck = bis.group(2).strip()
            if stueck:
                ergebnis[aktuell][-1].append(stueck)
    # Zeilen zusammensetzen, Trennstriche am Zeilenende aufloesen, Kapitelverweise entfernen
    fertig = {}
    for code, varianten in ergebnis.items():
        # Mehrere Module unter einer Nummer: jede Zeile, die mit ":" endet, beginnt eine neue Variante
        getrennt = []
        for teile in varianten:
            for s in teile:
                if s.endswith(':') or not getrennt:
                    getrennt.append([s])
                else:
                    getrennt[-1].append(s)
        texte = []
        for teile in getrennt:
            t = ''
            for s in teile:
                t = t[:-1] + s if t.endswith('-') and s[:1].islower() else (t + ' ' + s).strip()
            t = re.sub(r'\s*\[(?:Kap\.|[Cc]h\.|chap\.)[^\]]*\]', '', t)
            texte.append(re.sub(r'\s+', ' ', t).strip())
        fertig[code] = texte
    return fertig


def header_schreiben(daten, ziel):
    sp = 'de' if 'de' in daten else next(iter(daten))
    zeilen = ['// ERZEUGT von werkzeuge/fehlertexte_aus_handbuch.py aus der eigenen Anleitung - NICHT INS REPO.',
              '// Originaltexte (c) Max Weishaupt SE. Nur fuer die eigene Anlage.', '#pragma once',
              '#define FEHLERTEXTE_ORIGINAL 1', f'#define FEHLERTEXTE_ORIGINAL_EN {int(sp == "en")}  // Sprache dieser Texte',
              'namespace fehlercodes {',
              'struct Original { char typ; uint16_t nr; const char *text; };',
              'inline const Original ORIGINAL[] = {']
    for code, texte in sorted(daten[sp].items(), key=lambda kv: (kv[0][0], int(kv[0][2:].split('-')[0]))):
        t = ' / '.join(texte).replace('\\', '\\\\').replace('"', '\\"')
        nr = int(code[2:].split('-')[0])  # Bereich "W 1501-1532": Anfang genuegt, die Firmware sucht den Bereich
        zeilen.append(f"  {{'{code[0]}', {nr}, \"{t}\"}},")
    zeilen += ['};', '}  // namespace fehlercodes', '']
    ziel.write_text('\n'.join(zeilen), encoding='utf-8')


def main(argv):
    header = '--header' in argv
    pdfs = [a for a in argv if not a.startswith('--')]
    if not pdfs:
        print(__doc__)
        return 1
    daten = {}
    for pdf in pdfs:
        sp, zeilen, kopf = sprache_und_kapitel(text_der_pdf(pdf))
        daten[sp] = auslesen(zeilen, kopf, sp)
        print(f'{pdf}: Sprache {sp}, {len(daten[sp])} Codes')
    ziel = WURZEL / 'fehlertexte-original.json'
    ziel.write_text(json.dumps(daten, ensure_ascii=False, indent=1), encoding='utf-8')
    print(f'geschrieben: {ziel} (nicht ins Repo!)')
    if header:
        h = WURZEL / 'components' / 'weishaupt_can' / 'fehlertexte_original.h'
        header_schreiben(daten, h)
        print(f'geschrieben: {h} (nicht ins Repo!)')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
