Buzzer

Ich verwende den Buzzer um Bilder auf der Fotobox auszulösen. 
(getestet wurde Photobooth 4.99 und dslrBooth)

der Buzzer kann:
a)	einen http Befehl an einen Webserver senden
b)	einen mqtt String an einen Broker senden


Bei der 1. Inbetriebnahme macht der Buzzer einen AccessPoint auf („Buzzer“) über den man sich mit dem Handy verbinden kann. 
Über eine Weboberfläche kann man das WLAN auswählen mit dem sich der Buzzer über eine statische IP verbinden soll.

Weiters gibt man den http Befehl ein, welcher an den Webserver gesendet werden soll.

Zusätzlich kann man mqtt aktivieren (wer’s braucht). 

Wird die Eingabe gespeichert, dann startet der Buzzer neu und verbindet sich automatisch mit dem eingegebenen WLAN.
Die blaue LED leuchtet durchgehend, wenn sich der Buzzer im WLAN anmelden konnte.
Wurde aber zB eine falsche statische IP vorgegeben, dann ist der Buzzer zwar angemeldet, kann aber nichts machen. 
	

Um wieder auf den AccessPoint zu kommen:
a)	Buzzer ausgeschalten
b)	Buzzerknopf gedrückt halten und Buzzer einschalten


Handy wieder mit AP verbinden, Webseite aufmachen, Konfiguration ändern.

Konnte sich der Buzzer nicht mit dem WLAN verbinden, dann startet der AccessPoint automatisch, die blau LED blinkt.

Wenn alles richtig konfiguriert ist und man den roten Knopf drückt, dann:
a)	Befehl konnte erfolgreich zum Webserver gesendet werden =>  1x Piep.
b)	Webserver nicht erreichbar => 2x Piep.
c)	Mqtt aktiviert und Broker erreichbar => blaue LED blink 1x
d)	Mqtt aktiviert und Broker nicht erreichbar => blaue LED bleibt leuchtend (kein blink)
