# Erzeugt die Aufbau-Zeichnung bilder/aufbau.de.* und bilder/aufbau.en.* (SVG, PNG, PDF).
# Aufruf: python3 werkzeuge/zeichnung_aufbau.py   (braucht: pip install cairosvg, Schrift DejaVu Sans)
from pathlib import Path
import cairosvg

ZIEL = Path(__file__).resolve().parent.parent / 'bilder'

T = {
 'de': dict(
  titel='Weishaupt WTC mit WEM – wer redet mit wem',
  unter='Gas-Brennwertgerät WTC-GW (B-Reihe) mit eingebautem WEM-Systemgerät · ESP-Brücke aus diesem Repository',
  cloud='Internet – Weishaupt-Cloud', gsrv='Geräte-Server', xmpp='XMPP über TLS (STARTTLS)', zert='nur WEMs mit Gerätezertifikat',
  portal='WEM-Portal (Website)', p1='Anzeige, Fernbedienung,', p2='Datenaufzeichnung, Störmeldungen', intern1='intern bei', intern2='Weishaupt',
  browser='Browser / Weishaupt-App', ueberall='von überall', router='Router – Übergang Heimnetz ↔ Internet',
  tls='verschlüsselt (TLS 1.2)', auf1='der WEM baut die Verbindung auf,', auf2='Lebenszeichen etwa 1×/min',
  heim='Heimnetz – unverschlüsselt', buchse1='eine LAN-Buchse,', buchse2='ein Kabel', buchse3='eine LAN-Buchse am WEM',
  gw='WEM-Modbus-Gateway', gw1='Modbus TCP für Leittechnik,', gw2='liest den WEM zyklisch',
  app='Handy-App (app/)', a1='Anzeige, Schaltknöpfe', a2='Heizkreis / Warmwasser', mq='MQTT-Broker', m1='optional: InfluxDB, Grafana,', m2='Home Assistant',
  therme='THERME (Weishaupt WTC)', wem='WEM-SG Systemgerät (Knoten 1)', wem0='= Display / Bedieneinheit der Therme',
  w=['• <tspan font-weight="bold">Heizkurve</tspan> → rechnet Vorlauf-Soll','• Heizkreis / Warmwasser, Zeitprogramme','• Fehlerspeicher (bisher nur per JSON)','• Datenlogger fürs Portal','• JSON-Schnittstelle (ESP, Gateway)','• SD-Karte, Netzwerkbuchse'],
  wemf='Master des Systems', kes='KESSEL – Feuerungsautomat', kes0='WEM-FA-G (Knoten 2)',
  k=['• setzt den Vorlauf-Soll um','• Flamme, Gebläse, Gasventil','• Kesseltemperatur, Abgas, Volumenstrom','• Betriebsstunden, Brennerstarts','• eigene Grenzen (Abgas, Taktsperre,','  Pumpe), Flammenüberwachung','• fehlt der WEM: Fehler F 91'],
  c=['WEM schreibt in den Kessel:','Vorlauf-Soll, WW-Soll, Anforderung','WEM fragt den Kessel ~3×/s ab','(Abgas fürs Portal alle ~32 s)'],
  bus='CAN-Bus 50 kbit/s (CANopen-SDO/PDO) – unverschlüsselt', bus2='alle Teilnehmer hören alles · eine SDO-Antwort trägt max. 4 Byte', draehte='2 Drähte',
  esp='ESP-Brücke (WeAct CAN485)', e=['• hört alles mit','• liest einzeln per SDO','• schaltet über WEM-JSON,','  prüft das Ergebnis am Bus'], e5='am Bus nur Leseanfragen',
  wlan='WLAN · MQTT', wlan2='(IP wie LAN)', json='HTTP / JSON über WLAN + Router – nur Schaltbefehle', lan='LAN · JSON',
  rf='Raumfühler / Raumgerät', rf1='WEM-RF, WEM-RG · im Wohnraum', rf2='am selben CAN-Bus',
  js=['JSON = ferngesteuerter CAN-Zugang über LAN (CanApiJson)','Die Anfrage nennt ein CAN-Objekt, z. B. Modul 07 · 2533/02 = Knoten 2.','Der WEM übersetzt, liest oder schreibt am Bus und antwortet als JSON.','Er ist Übersetzer und Türsteher: nur freigegebene Objekte,','am Kessel ist nichts schreibbar. Eigene Objekte beantwortet er direkt.'],
  l1='verschlüsselt', l2='Heimnetz, unverschlüsselt', l3='CAN-Bus', l4='durch den Router', l5='optional', l6='aus diesem Repository',
  beleg='Belege: CAN-Mitschnitt, Netzwerkmitschnitt, Verbindungstest zum Geräte-Server, Weishaupt-Anleitungen WTC-GW 15…32-B (83307201) und WEM-Modbus-Gateway (83325901).'),
 'en': dict(
  titel='Weishaupt WTC with WEM – who talks to whom',
  unter='Gas condensing boiler WTC-GW (B series) with built-in WEM system unit · ESP bridge from this repository',
  cloud='Internet – Weishaupt cloud', gsrv='Device server', xmpp='XMPP over TLS (STARTTLS)', zert='only WEMs with a device certificate',
  portal='WEM portal (website)', p1='display, remote control,', p2='data logging, fault messages', intern1='internal at', intern2='Weishaupt',
  browser='Browser / Weishaupt app', ueberall='from anywhere', router='Router – home network ↔ internet',
  tls='encrypted (TLS 1.2)', auf1='the WEM opens the connection,', auf2='keep-alive about once a minute',
  heim='Home network – unencrypted', buchse1='one LAN socket,', buchse2='one cable', buchse3='one LAN socket on the WEM',
  gw='WEM Modbus gateway', gw1='Modbus TCP for automation,', gw2='polls the WEM cyclically',
  app='Phone app (app/)', a1='display, switch buttons', a2='heating circuit / hot water', mq='MQTT broker', m1='optional: InfluxDB, Grafana,', m2='Home Assistant',
  therme='BOILER UNIT (Weishaupt WTC)', wem='WEM-SG system unit (node 1)', wem0='= display / control panel of the unit',
  w=['• <tspan font-weight="bold">heating curve</tspan> → flow setpoint','• heating circuit / hot water, schedules','• fault memory (so far JSON only)','• data logger for the portal','• JSON interface (ESP, gateway)','• SD card, network socket'],
  wemf='system master', kes='BOILER – burner control', kes0='WEM-FA-G (node 2)',
  k=['• applies the flow setpoint','• flame, fan, gas valve','• boiler temp., flue gas, flow rate','• operating hours, burner starts','• own limits (flue gas, anti-cycling,','  pump), flame supervision','• WEM missing: fault F 91'],
  c=['WEM writes to the boiler:','flow setpoint, DHW setpoint, demand','WEM polls the boiler ~3×/s','(flue gas for the portal every ~32 s)'],
  bus='CAN bus 50 kbit/s (CANopen SDO/PDO) – unencrypted', bus2='every node hears everything · one SDO reply carries max. 4 bytes', draehte='2 wires',
  esp='ESP bridge (WeAct CAN485)', e=['• listens to everything','• reads single values (SDO)','• switches via WEM JSON,','  verifies the result on the bus'], e5='only read requests on the bus',
  wlan='Wi-Fi · MQTT', wlan2='(IP, same as LAN)', json='HTTP / JSON via Wi-Fi + router – switch commands only', lan='LAN · JSON',
  rf='Room sensor / room unit', rf1='WEM-RF, WEM-RG · in the living area', rf2='on the same CAN bus',
  js=['JSON = remote CAN access over LAN (CanApiJson)','A request names a CAN object, e.g. module 07 · 2533/02 = node 2.','The WEM translates, reads or writes on the bus and answers as JSON.','It is translator and gatekeeper: only permitted objects,','nothing on the boiler is writable. Its own objects it answers directly.'],
  l1='encrypted', l2='home network, unencrypted', l3='CAN bus', l4='through the router', l5='optional', l6='from this repository',
  beleg='Sources: CAN capture, network capture, connection test to the device server, Weishaupt manuals WTC-GW 15…32-B (83307201) and WEM Modbus gateway (83325901).'),
}

def svg(t):
    W = lambda s: s.replace('&', '&amp;')
    lines = lambda x, y0, dy, arr, extra='': ''.join(f'<text x="{x}" y="{y0+i*dy}"{extra}>{a}</text>\n' for i, a in enumerate(arr))
    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="1190" height="1030" viewBox="0 0 1190 1030" font-family="DejaVu Sans" font-size="13">
<defs>
 <marker id="pr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="#c0392b"/></marker>
 <marker id="pg" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="#1e8449"/></marker>
 <marker id="pb" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="#2e86c1"/></marker>
</defs>
<rect width="1190" height="1030" fill="#fff"/>
<text x="40" y="34" font-size="20" font-weight="bold">{t['titel']}</text>
<text x="40" y="54" fill="#555">{t['unter']}</text>
<rect x="30" y="72" width="830" height="150" rx="10" fill="#eef3fb" stroke="#7f9cc7"/>
<text x="45" y="92" font-weight="bold" fill="#34527f">{t['cloud']}</text>
<rect x="60" y="105" width="320" height="100" rx="6" fill="#fff" stroke="#34527f"/>
<text x="75" y="128" font-weight="bold">{t['gsrv']}</text>
<text x="75" y="148">xmpp.wemportal.de · Port 5222</text>
<text x="75" y="166">{t['xmpp']}</text>
<text x="75" y="186" fill="#555" font-size="12">{t['zert']}</text>
<rect x="510" y="105" width="320" height="100" rx="6" fill="#fff" stroke="#34527f"/>
<text x="525" y="128" font-weight="bold">{t['portal']}</text>
<text x="525" y="148">wemportal.com</text>
<text x="525" y="166" fill="#555" font-size="12">{t['p1']}</text>
<text x="525" y="182" fill="#555" font-size="12">{t['p2']}</text>
<line x1="380" y1="155" x2="510" y2="155" stroke="#34527f" stroke-dasharray="6,4"/>
<text x="392" y="146" font-size="11" fill="#34527f">{t['intern1']}</text>
<text x="392" y="172" font-size="11" fill="#34527f">{t['intern2']}</text>
<rect x="910" y="105" width="240" height="100" rx="6" fill="#fff" stroke="#555"/>
<text x="925" y="128" font-weight="bold">{t['browser']}</text>
<text x="925" y="148">{t['ueberall']}</text>
<line x1="910" y1="155" x2="832" y2="155" stroke="#c0392b" stroke-width="2.5" marker-end="url(#pr)"/>
<text x="838" y="146" fill="#c0392b" font-size="12">HTTPS</text>
<rect x="30" y="250" width="1120" height="34" rx="6" fill="#f4f4f4" stroke="#888"/>
<text x="595" y="272" font-weight="bold" text-anchor="middle">{t['router']}</text>

<line x1="200" y1="398" x2="200" y2="207" stroke="#c0392b" stroke-width="3" marker-end="url(#pr)"/>
<circle cx="200" cy="267" r="6" fill="#c0392b"/>
<text x="212" y="235" fill="#c0392b" font-size="12">{t['tls']}</text>
<text x="212" y="322" fill="#c0392b" font-size="12">{t['auf1']}</text>
<text x="212" y="338" fill="#c0392b" font-size="12">{t['auf2']}</text>
<text x="40" y="306" font-weight="bold" fill="#1e8449">{t['heim']}</text>

<rect x="445" y="300" width="200" height="75" rx="6" fill="#fff" stroke="#777" stroke-dasharray="6,4"/>
<text x="458" y="322" font-weight="bold">{t['gw']}</text>
<text x="458" y="341" font-size="12" fill="#555">{t['gw1']}</text>
<text x="458" y="359" font-size="12" fill="#555">{t['gw2']}</text>
<rect x="665" y="300" width="190" height="75" rx="6" fill="#efe3f7" stroke="#7d3c98" stroke-width="2"/>
<text x="678" y="322" font-weight="bold">{t['app']}</text>
<text x="678" y="341" font-size="12" fill="#555">{t['a1']}</text>
<text x="678" y="359" font-size="12" fill="#555">{t['a2']}</text>
<rect x="900" y="300" width="250" height="75" rx="6" fill="#fff" stroke="#1e8449"/>
<text x="913" y="322" font-weight="bold">{t['mq']}</text>
<text x="913" y="341" font-size="12" fill="#555">{t['m1']}</text>
<text x="913" y="359" font-size="12" fill="#555">{t['m2']}</text>
<line x1="857" y1="337" x2="898" y2="337" stroke="#1e8449" stroke-width="2" marker-start="url(#pg)" marker-end="url(#pg)"/>

<rect x="30" y="420" width="790" height="400" rx="12" fill="#fdf6ec" stroke="#b9770e" stroke-width="2"/>
<text x="805" y="442" font-weight="bold" fill="#b9770e" text-anchor="end">{t['therme']}</text>
<rect x="60" y="452" width="360" height="200" rx="6" fill="#fff" stroke="#333" stroke-width="1.5"/>
<text x="75" y="476" font-weight="bold">{t['wem']}</text>
<text x="75" y="494" font-size="12" fill="#555">{t['wem0']}</text>
{lines(75, 516, 20, t['w'])}<text x="75" y="638" font-size="12" fill="#555">{t['wemf']}</text>
<rect x="480" y="452" width="320" height="200" rx="6" fill="#fff" stroke="#333" stroke-width="1.5"/>
<text x="495" y="476" font-weight="bold">{t['kes']}</text>
<text x="495" y="494" font-size="12" fill="#555">{t['kes0']}</text>
{lines(495, 516, 19, t['k'])}
<line x1="420" y1="500" x2="478" y2="500" stroke="#2e86c1" stroke-width="2" marker-end="url(#pb)"/>
<line x1="478" y1="535" x2="420" y2="535" stroke="#2e86c1" stroke-width="2" marker-end="url(#pb)"/>

<!-- eine LAN-Buchse: Cloud, ESP-JSON und Gateway teilen sich das Kabel -->
<polyline points="940,685 940,398 204,398" fill="none" stroke="#1e8449" stroke-width="2.5"/>
<line x1="500" y1="375" x2="206" y2="396" stroke="#777" stroke-width="2" stroke-dasharray="6,4"/>
<text x="330" y="372" fill="#777" font-size="11">{t['lan']}</text>
<line x1="200" y1="398" x2="200" y2="450" stroke="#333" stroke-width="5"/>
<circle cx="200" cy="398" r="7" fill="#333"/>
<text x="190" y="394" font-size="11" font-weight="bold" fill="#333" text-anchor="end">{t['buchse1']}</text>
<text x="190" y="408" font-size="11" fill="#333" text-anchor="end">{t['buchse2']}</text>
<text x="520" y="392" fill="#1e8449" font-size="12">{t['json']}</text>

<line x1="50" y1="745" x2="880" y2="745" stroke="#2e86c1" stroke-width="6"/>
<line x1="240" y1="652" x2="240" y2="745" stroke="#2e86c1" stroke-width="3"/>
<line x1="640" y1="652" x2="640" y2="745" stroke="#2e86c1" stroke-width="3"/>
{lines(255, 672, 16, t['c'][:2], ' font-size="12" fill="#2e86c1"')}{lines(255, 708, 16, t['c'][2:], ' font-size="12" fill="#2e86c1"')}
<text x="60" y="775" font-weight="bold" fill="#2e86c1">{t['bus']}</text>
<text x="60" y="793" font-size="12" fill="#2e86c1">{t['bus2']}</text>
<text x="812" y="735" font-size="12" fill="#2e86c1">{t['draehte']}</text>

<line x1="760" y1="745" x2="760" y2="845" stroke="#2e86c1" stroke-width="3" stroke-dasharray="7,4"/>
<rect x="560" y="845" width="290" height="70" rx="6" fill="#fff" stroke="#777" stroke-dasharray="6,4"/>
<text x="573" y="867" font-weight="bold">{t['rf']}</text>
<text x="573" y="886" font-size="12" fill="#555">{t['rf1']}</text>
<text x="573" y="904" font-size="12" fill="#555">{t['rf2']}</text>

<rect x="880" y="685" width="270" height="135" rx="6" fill="#efe3f7" stroke="#7d3c98" stroke-width="2"/>
<text x="895" y="709" font-weight="bold">{t['esp']}</text>
{lines(895, 729, 20, t['e'][:3])}<text x="895" y="787">{t['e'][3]}</text>
<text x="895" y="807" font-size="12" fill="#555">{t['e5']}</text>
<line x1="1025" y1="685" x2="1025" y2="378" stroke="#1e8449" stroke-width="2.5" marker-start="url(#pg)" marker-end="url(#pg)"/>
<text x="1035" y="520" fill="#1e8449" font-size="12">{t['wlan']}</text>
<text x="1035" y="536" fill="#1e8449" font-size="11">{t['wlan2']}</text>

<rect x="30" y="832" width="510" height="104" rx="8" fill="#eafaf1" stroke="#1e8449"/>
<text x="45" y="853" font-weight="bold" fill="#1e8449">{t['js'][0]}</text>
{lines(45, 873, 15, t['js'][1:], ' font-size="12"')}
<rect x="40" y="950" width="40" height="18" rx="3" fill="none" stroke="#777" stroke-dasharray="6,4"/><text x="88" y="964">{t['l5']}</text>
<rect x="200" y="950" width="40" height="18" rx="3" fill="#efe3f7" stroke="#7d3c98" stroke-width="2"/><text x="248" y="964" font-weight="bold" fill="#7d3c98">{t['l6']}</text>
<line x1="560" y1="959" x2="600" y2="959" stroke="#2e86c1" stroke-width="6"/><text x="608" y="963">{t['l3']}</text>
<circle cx="740" cy="959" r="6" fill="#c0392b"/><text x="753" y="963">{t['l4']}</text>
<line x1="40" y1="988" x2="80" y2="988" stroke="#c0392b" stroke-width="3"/><text x="88" y="992">{t['l1']}</text>
<line x1="200" y1="988" x2="240" y2="988" stroke="#1e8449" stroke-width="3"/><text x="248" y="992">{t['l2']}</text>
<circle cx="565" cy="988" r="7" fill="#333"/><text x="580" y="992">{t['buchse3']}</text>
<text x="40" y="1018" font-size="11" fill="#555">{W(t['beleg'])}</text>
</svg>
'''

for lang in ('de', 'en'):
    p = ZIEL / f'aufbau.{lang}.svg'
    p.write_text(svg(T[lang]))
    cairosvg.svg2pdf(url=str(p), write_to=str(p.with_suffix('.pdf')))
    cairosvg.svg2png(url=str(p), write_to=str(p.with_suffix('.png')), scale=2)
