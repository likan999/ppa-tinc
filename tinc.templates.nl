Template: tinc/restart_on_upgrade
Description: Start tinc opnieuw na iedere upgrade?
 Je kunt kiezen of ik de tinc daemon opnieuw moet starten iedere keer
 als je een nieuwe versie van het pakket installeert.
 .
 Soms wil je dit niet doen, bij voorbeeld als je de upgrade uitvoert
 over een tunnel die met tinc is gemaakt.  Het stoppen van tinc
 resulteert dan waarschijnlijk is een dode verbinding, en tinc wordt
 dan misschien niet opnieuw gestart.
 .
 Als je nee antwoordt, moet je zelf tinc opnieuw starten na een
 upgrade, door `/etc/init.d/tinc restart' in te tiepen wanneer het
 goed uitkomt.
