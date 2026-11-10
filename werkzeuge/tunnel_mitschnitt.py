#!/usr/bin/env python3
"""Nimmt den Mitschnitt des CAN-Tunnels an (Paket can-tunnel, VERSUCHE.md Abschnitt 10b) und schreibt jede Zeile
mit der Empfangszeit auf die Standardausgabe oder in eine Datei. Laeuft auf dem Rechner, dessen Adresse die Boards
als tunnel_log eingetragen haben; braucht nur Python 3.

  python3 werkzeuge/tunnel_mitschnitt.py                     alles anzeigen
  python3 werkzeuge/tunnel_mitschnitt.py --datei tunnel.txt  zusaetzlich anhaengen an tunnel.txt
  python3 werkzeuge/tunnel_mitschnitt.py --nur B             nur die Zeilen des Boards mit dem Kuerzel B
  python3 werkzeuge/tunnel_mitschnitt.py --ohne-stat         ohne die Statuszeilen (alle 10 s)

Zeilen der Boards:   <Kuerzel> <ms seit Start des Boards> <R|T|X> <CAN-ID>[r]:<Daten hex>
  R = auf dem eigenen Bus empfangen (und an die Gegenstelle geschickt)
  T = von der Gegenstelle erhalten und auf den eigenen Bus gesendet
  X = Senden auf den eigenen Bus abgelehnt;  r hinter der ID = RTR-Frame
Statuszeile:         <Kuerzel> STAT <ms> vom_bus=… zum_bus=… udp_verloren=… can_fehler=… gesperrt=… bus_off=…

Der Mitschnitt ist unverschluesselt und kommt per UDP: Zeilen koennen fehlen, und der Rechner muss den Port
(Vorgabe 47012, PORT_LOG in can_tunnel.h) in seiner Firewall fuer das Heimnetz annehmen.
"""

import argparse
import socket
import time


def main() -> None:
    ap = argparse.ArgumentParser(description="Mitschnitt des CAN-Tunnels per UDP annehmen")
    ap.add_argument("--port", type=int, default=47012, help="UDP-Port (Vorgabe 47012)")
    ap.add_argument("--adresse", default="0.0.0.0", help="eigene Adresse, auf der gelauscht wird (Vorgabe: alle)")
    ap.add_argument("--datei", help="Zeilen zusaetzlich an diese Datei anhaengen")
    ap.add_argument("--nur", help="nur Zeilen dieses Board-Kuerzels (tunnel_name)")
    ap.add_argument("--ohne-stat", action="store_true", help="Statuszeilen weglassen")
    a = ap.parse_args()

    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind((a.adresse, a.port))
    datei = open(a.datei, "a", encoding="ascii", errors="replace") if a.datei else None
    try:
        while True:
            paket, _ = s.recvfrom(2048)
            t = time.time()
            zeit = time.strftime("%H:%M:%S", time.localtime(t)) + ".%03d" % (t % 1 * 1000)
            for zeile in paket.decode("ascii", "replace").splitlines():
                felder = zeile.split()
                if not felder or (a.nur and felder[0] != a.nur):
                    continue
                if a.ohne_stat and len(felder) > 1 and felder[1] == "STAT":
                    continue
                aus = f"{zeit} {zeile}"
                print(aus, flush=True)
                if datei:
                    datei.write(aus + "\n")
                    datei.flush()
    except KeyboardInterrupt:
        pass
    finally:
        if datei:
            datei.close()


if __name__ == "__main__":
    main()
