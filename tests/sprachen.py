#!/usr/bin/env python3
"""Prueft, dass jede Doku im Wurzelverzeichnis deutsch und englisch vorliegt und beide gleich aufgebaut sind:
X.md und X.en.md vorhanden, Sprachhinweis am Kopf beider Dateien, gleiche Folge der Ueberschriften-Ebenen,
gleich viele Codebloecke, gleiche Folge der <!--ab-werk:...-->-Marken.
Die deutsche Fassung ist massgeblich, die englische ihre Uebersetzung - wer eine aendert, aendert beide."""
import glob, os, re, sys

WURZEL = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MARKE = re.compile(r'<!--ab-werk:([a-z0-9_]+)-->')


def aufbau(pfad):
    ebenen, zaeune, marken, im_code = [], 0, [], False
    with open(pfad, encoding='utf-8') as f:
        zeilen = f.read().split('\n')
    for z in zeilen:
        if z.lstrip().startswith('```'):
            im_code = not im_code
            zaeune += 1
            continue
        marken += MARKE.findall(z)
        if not im_code and re.match(r'#{1,6}\s', z):
            ebenen.append(len(z) - len(z.lstrip('#')))
    return zeilen, ebenen, zaeune, marken


def main():
    fehler = []
    alle = sorted(os.path.basename(p) for p in glob.glob(os.path.join(WURZEL, '*.md')))
    deutsch = [n for n in alle if not n.endswith('.en.md')]
    for n in alle:
        if n.endswith('.en.md') and n[:-6] + '.md' not in deutsch:
            fehler.append(f'{n}: deutsche Fassung {n[:-6]}.md fehlt')
    for de in deutsch:
        en = de[:-3] + '.en.md'
        if en not in alle:
            fehler.append(f'{de}: englische Fassung {en} fehlt')
            continue
        zd, ed, cd, md = aufbau(os.path.join(WURZEL, de))
        ze, ee, ce, me = aufbau(os.path.join(WURZEL, en))
        if not any(f']({en})' in z for z in zd[:6]):
            fehler.append(f'{de}: Hinweis auf {en} fehlt am Kopf')
        if f']({de})' not in ze[0]:
            fehler.append(f'{en}: erste Zeile nennt nicht das deutsche Original {de}')
        if ed != ee:
            i = next((k for k, (a, b) in enumerate(zip(ed, ee)) if a != b), min(len(ed), len(ee)))
            fehler.append(f'{en}: Ueberschriften weichen ab ({len(ed)} deutsch, {len(ee)} englisch, erste Abweichung bei Nr. {i + 1})')
        if cd != ce:
            fehler.append(f'{en}: Codebloecke weichen ab ({cd // 2} deutsch, {ce // 2} englisch)')
        if md != me:
            fehler.append(f'{en}: ab-werk-Marken weichen ab ({len(md)} deutsch, {len(me)} englisch)')
    for f in fehler:
        print(f)
    print(f'{len(deutsch)} Sprachpaare geprueft, {len(fehler)} Fehler')
    sys.exit(1 if fehler else 0)


if __name__ == '__main__':
    main()
