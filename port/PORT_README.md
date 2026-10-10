# WanagoBack voor New Nintendo 3DS — prototype 0.5

De ARM-code van deze versie is gebouwd via GitHub Actions. De CIA wordt lokaal verpakt met de oorspronkelijke Windows-textures. Dit is een prototype: installatie, audio, video en touchscreen zijn nog niet op een echte console getest.

## Wat deze eerste port bevat

- Touchscreen-interface met de originele Windows-textures: achtergrond, personagekaarten, navigatieknoppen, geel detailvenster, categorie-iconen en Lilita One-lettertype.
- Personagekeuze, horizontaal verschuifbare videorijen, detailvenster, lokale favorieten/playlist, zoeken en instellingen.
- Laadanimatie met dezelfde wolken/Plop-render en tijdsduren voor de wolken, bob-beweging en tekstpuls. Nieuwe native implementatie: geen browser op de 3DS.
- Video bovenaan; afspelen/pauzeren, tijdlijn, terug en 10 seconden spoelen op het touchscreen.
- prepare_sd.py maakt nu ook thumbnails. Voer het opnieuw uit voor bestaande voorbereide video's; die video's worden dan niet opnieuw geconverteerd.
- Haalt je bestaande https://raw.githubusercontent.com/WanagoBack/testing/main/library.json op; bewaart een kopie op SD voor offline gebruik. Maximaal 512 catalogusposities en 1 MiB JSON.
- D-pad, A en touchscreen om te kiezen; X om te verversen, START om vanuit de bibliotheek af te sluiten.
- Speelt voorbereide SD-video's met de decoder van Video player for 3DS. Hardwaredecodering staat upstream standaard aan op New-modellen.
- Optioneel `url3ds`: een directe HTTPS-link naar een geschikte MP4. Die wordt eerst volledig naar SD gedownload. Daarna druk je A om te kijken. B annuleert een download. Er is nog geen gedeeltelijke downloadhervatting of directe streaming.
- Afzonderlijke app-ID 0xEC610. Dit is een gekozen homebrew-ID, geen officieel gereserveerde registratie. De upstream zelf-updater staat uit.

## Nieuw in 0.5

Tijdens video worden de menu-textures en thumbnailcache na GPU-synchronisatie vrijgegeven; alleen de bediening en het lettertype blijven staan. Bij terugkeer worden de menu-assets herladen. De oude onderliggende debug/bedieningsinterface wordt tijdens native weergave niet meer getekend (foutmeldingen blijven bereikbaar). De herbufferdrempel is 12 in plaats van 48 gedecodeerde frames; dit verlaagt wachttijd, maar kan op een trage verbinding vaker herbufferen. Dit zijn concrete aanpassingen, geen op hardware gemeten FPS-winst.

3D-weergave en de automatische 800px-modus zijn uitgeschakeld. De renderer blijft op 400x240 in 2D, ongeacht de 3D-schuif. Voor native afspelen wordt slechts één videotrack gedecodeerd.

Onderzoek naar de gemelde crash bij In de kijker: de netwerklees- en opentaken hadden elk 64 KiB stack, terwijl ze nu ook HTTPS uitvoeren. Die taken krijgen 256 KiB. Dit is een voorzorg tegen stackuitputting, geen bevestigde diagnose van de crash. `3ds/WanagoBack/last_playback.txt` bewaart titel en laatste opstartstap (geen bronlinks/sleutels). De specifieke crash is nog niet op hardware gereproduceerd.

De oorspronkelijke fullscreen-timer dimde en schakelde het onderste scherm uit. Bij native WanagoBack-video blijft het touchscreen nu ingeschakeld, met dezelfde helderheid als het bovenste scherm. Terug, pauze en spoelen blijven zichtbaar.

Favorieten en maximaal acht benoemde playlists worden per profiel opgeslagen met reservekopie en herstel. Open het hartje bovenaan voor Favorieten & Playlists. Maak, open, bewerk of verwijder je lijsten daar. Bij maken/bewerken kies je uit dezelfde 15 originele playlist-plaatjes als in de Windows-app. Playlists staan niet meer in Instellingen. In het gele videovenster opent het playlist-icoon een keuzelijst; tik op een lijst om de video toe te voegen of eruit te verwijderen. Een ster geeft aan dat de video al in die lijst staat. De video's staan in toevoegvolgorde. Een playlist start nog niet automatisch de volgende video. Oude favorieten en de oude enkele playlist worden bij het eerste gebruik overgenomen. Mislukte wijzigingen worden teruggedraaid en krijgen een zichtbare SD-foutmelding.

De videospeler slaat niet-sleutelframes over als de decoder achterloopt en gebruikt multithreading. Bij codecs die verkleind decoderen ondersteunen wordt automatisch een kleinere decodeerresolutie gekozen, met behoud van ten minste 240 beeldlijnen. Hardwaredecodering blijft beschikbaar; dit voegt geen realtime omzetting voor zware H.264/VP9-video's toe.

Beschikbare vooraf gemaakte MEGA-versies kunnen in `sources3ds` staan. De 3DS kiest de laagste positieve hoogte en daarna de laagste bitrate. `megaUrl3ds` blijft de terugvaloptie, `megaUrl` de oorspronkelijke bron. Een lichte `url3ds` krijgt nu voorrang op de oorspronkelijke MEGA-bron wanneer er geen lichte MEGA-versie is. Een voorbereide SD-video blijft eerste keus.

```json
{"title":"Voorbeeld","megaUrl":"ORIGINELE_LINK","sources3ds":[
 {"height":480,"bitrate":900000,"megaUrl":"VOLLEDIGE_MEGA_LINK_480"},
 {"height":240,"bitrate":350000,"megaUrl":"VOLLEDIGE_MEGA_LINK_240"}
]}
```

De bestaande online catalogus is niet gewijzigd en heeft daardoor niet vanzelf meerdere kwaliteitsversies. Voeg werkelijke vooraf omgezette uploads toe; verzonnen kwaliteits-URL's werken niet.

## Herstel in 0.4

De catalogueparser is vervangen: de eerdere JSON-padzoeker leverde bij de huidige catalogus nul resultaten. De nieuwe parser is met alle 21 huidige posities getest. De actuele catalogus zit ook in ROMFS; de app probeert daarna de online versie en bewaart een SD-cache. Bij netwerkfalen blijft de bestaande lijst zichtbaar. In Instellingen staat de netwerkstatus.

SD-paden gebruiken expliciet `sdmc:/`. Opslaan werkt ook wanneer de SD-driver geen bestaand bestand met `rename` kan vervangen: het oude bestand wordt als `.bak` bewaard, met herstel bij een mislukte vervanging. Profielen kunnen uit die reservekopie herstellen. Deze fout is getest met een gesimuleerde SD-driver die overschrijven weigert.

Menumuziek ondersteunt `3ds/WanagoBack/music/menu.wav` (PCM 16-bit, mono/stereo, 8–48 kHz). De muziek loopt in menu's, stopt vóór een video en hervat bij terugkeer. Er is een aan/uit-knop in Instellingen. Je aangeleverde Wanagogo.mp3 is als herhalende menumuziek ingebouwd; er is geen losse SD-installatie voor nodig. Een eigen SD-bestand menu.wav heeft voorrang.

De compacte videopreset gebruikt H.264 baseline, maximaal 400×240, 25 fps, CRF 28, maximaal 500 kbit/s video en 64 kbit/s AAC. `prepare_sd.py` converteert bestanden voor de SD-kaart. Een vooraf gemaakte lichtere MEGA-upload kan als `megaUrl3ds` naast `megaUrl` in library.json worden gezet; de 3DS kiest die versie. De app maakt een zware stream niet onderweg kleiner. Bestaande `megaUrl` blijft de Windows-bron en de identificatie voor de SD-cache.

Dit is nog geen volledige Windows-port: pc-games, boeken, alle animaties en automatische MEGA-thumbnails ontbreken nog. Deze release herstelt profielen en catalogus en voegt de muziekvoorziening en de lichte-video-route toe. Hardwarevalidatie blijft nodig.

## Nieuw in 0.3

- Rechtstreeks afspelen van publieke MEGA-bestandslinks uit `megaUrl`, zonder vooraf downloaden naar SD. Kleine HTTPS-blokken worden op verzoek opgehaald en lokaal met AES-CTR ontsleuteld. Twee geheugenblokken van 512 KiB; geen volledige film in RAM of op SD. De decoder leest netwerkdata op zijn bestaande achtergrondthread.
- Door- en terugspoelen via willekeurige byteposities, ook bij MP4-bestanden met metadata achteraan. Maximaal drie pogingen bij netwerkfouten; overdrachtslimieten worden gemeld en niet omzeild.
- Terug met B of de terugknop, ook vanuit de speler en tijdens laden. Schermgeschiedenis herstelt de vorige categorie en positie.
- Vier lokale profielen met naam, de twaalf oorspronkelijke avatars, bewerken en verwijderen met bevestiging. Profielen staan op SD; favorieten en playlist zijn per profiel. De bestaande favorieten blijven bij het standaardprofiel.
- Alle dertien oorspronkelijke personagecategorieën blijven selecteerbaar, ook als de online catalogus nog geen video's voor die categorie bevat.

## Grenzen

De streamingtransportcode is op de host getest met een echte cataloguslink: metadata, ontsleuteling, lezen over blokgrenzen, willekeurig zoeken, EOF en annuleren. De ontsleutelde bytes zijn onafhankelijk vergeleken. Een HTTPS-testserver controleerde daarnaast bestanden groter dan 2 GiB, foutieve metadata, verwijderde links, korte antwoorden en overdrachtslimieten. De ARM-build en CIA-verpakking zijn afzonderlijk gecontroleerd. De complete app is nog niet op een echte New 3DS getest.

Streaming ondersteunt publieke losse bestandslinks (`https://mega.nz/file/HANDLE#KEY` en oude `#!HANDLE!KEY`-links), geen mappen of accountlogin. HTTPS-certificaten worden gecontroleerd. Bij gedeeltelijk lezen kan MEGA's volledige bestands-MAC niet worden gecontroleerd; er wordt geen cryptografische volledige-bestandsverificatie geclaimd.

Het videoformaat en de bitrate moeten binnen de decoder- en wifi-capaciteit van de New 3DS passen. Dit converteert geen 1080p/4K-video tijdens het streamen. Gebruik bij te zware bestanden een lichtere MP4 op MEGA of de bestaande SD-voorbereiding. De grootte van een film alleen zegt niet of afspelen lukt. De geteste catalogusvideo Klus op dieet gebruikt VP9 op 1250×720; daarvoor wordt geen vloeiende weergave op de New 3DS beloofd.

Thumbnails rechtstreeks uit MEGA, pc-games en boeken zijn nog niet geport. Bestaande voorbereide SD-video's en thumbnails blijven werken. Op 320×240 zijn tekstmaten en aantallen zichtbare kaarten aangepast; dit is geen pixelidentieke desktop-render.

## Installeren en bouwen

Installeer WanagoBack_New3DS_0.5.cia met FBI op een New 3DS, New 3DS XL of New 2DS XL met homebrew-ondersteuning. Deze versie gebruikt hetzelfde app-ID als eerdere versies, met een hogere pakketversie. Je SD-video's blijven op dezelfde plek.

De code staat op de branch `codex/wanagoback-3ds` van `WanagoBack/testing`. De workflow daar compileert alleen code. De originele textures staan uitsluitend in dit bronpakket en het lokaal verpakte CIA-bestand; het 3DSX-bestand uit die workflow bevat deze textures niet.

Voor een volledige lokale build: installeer devkitPro/devkitARM en de vereiste 3DS-bibliotheken (zie Makefile en library/_devkitpro_install.md), voer `python3 tools/install_cia_tools.py` uit en vervolgens `make cia` met `tools/bin` in PATH. Het pakket bevat de volledige gewijzigde frontend, originele upstreamcode, vooraf gebouwde decoderbibliotheken en ROMFS-assets. De officiële devkitPro devkitarm-container is gebruikt voor de ARM-compilatie. De gebruikte compiler staat in BUILD_VERSIONS.txt.

## Video's voorbereiden — zonder thuisserver

Download de gewenste video's zelf en zet ze in één map. Geef iedere video de titel uit `library.json` als bestandsnaam, bijvoorbeeld `Klus op dieet.mp4`. Python 3 en FFmpeg moeten op de computer geïnstalleerd zijn.

Voorbeeld op macOS (pas je mapnamen aan):

```sh
python3 tools/prepare_sd.py --videos "$HOME/Movies/WanagoBack" --sd "$HOME/Desktop/WanagoBack-SD"
```

Kopieer daarna de gemaakte map `3ds` naar de root van je 3DS-SD-kaart. De computer hoeft tijdens het kijken niet aan te staan. De tool maakt H.264 baseline/AAC-video's van 400×240 met behoud van beeldverhouding. Bestaande voorbereide bestanden worden overgeslagen; `--overwrite` vervangt ze. Met `--library pad/naar/library.json` kun je ook zonder internet voorbereiden.

Geen lokale video gevonden? De tool meldt de ontbrekende titels. Meerdere lokale bestanden met dezelfde titel worden gemeld en overgeslagen. Met `--mega` kan de tool ontbrekende publieke MEGA-bestanden zelf downloaden en omzetten. Installeer hiervoor `python3 -m pip install cryptography` en gebruik bijvoorbeeld:

```sh
python3 tools/prepare_sd.py --mega --sd WanagoBack-SD
```

Dit kan lang duren en verbruikt de normale MEGA-overdrachtsruimte; limieten worden niet omzeild. Na een fout kun je hetzelfde commando opnieuw uitvoeren: voltooide video's worden overgeslagen. De downloadhelper controleert HTTPS en bestandsgrootte, maar niet MEGA's volledige bestands-MAC. De nieuwe automatische downloadhelper is met een lokale versleutelde fixture getest; de conversie is ook op de aangeleverde video getest.

Alleen menumuziek voorbereiden:

```sh
python3 tools/prepare_sd.py --music lied.mp3 --sd WanagoBack-SD
```

De optionele SD-voorbeeldzip bevat uitsluitend de aangeleverde video “Wat is Wanagogo?” in compacte kwaliteit met thumbnail. De andere catalogusvideo's worden daar niet mee omgezet.

## Optionele rechtstreekse download

Voeg per video een `url3ds` toe naast bestaande velden:

```json
{"videos":[{"folder":"Plop","title":"Voorbeeld","url3ds":"https://jouw-host.example/video-400x240.mp4"}]}
```

Gebruik een daadwerkelijke videolink, geen webpagina/deellink. Alleen HTTPS met certificaatcontrole is toegestaan. Downloaden vereist voldoende vrije SD-ruimte voor het hele bestand. De app doet geen omzetting op de console; gebruik vooraf een geschikt H.264/AAC-bestand.

## Validatie en resterende test

Gecontroleerd: geslaagde ARM-compilatie, ELF-architectuur, lokaal verpakken met makerom en CIA-sectiegrenzen. De SD-voorbereiding is getest met een gegenereerde korte video en bijbehorende thumbnail. De originele textures zijn met hun herkomst vastgelegd in romfs/wgb/provenance.json. Dit bewijst nog niet dat alles op hardware werkt.

Test na de eerste geslaagde build: opstarten, catalogus laden, offline opstart met SD-catalogus, één voorbereide video afspelen met geluid, teruggaan naar bibliotheek, download annuleren en opnieuw starten. Begin met een korte video.

## Herkomst en licenties

Gebaseerd op Core-2-Extreme/Video_player_for_3DS, commit 3b326d272344087017549f5f1ac5eadf06e413b7 (https://github.com/Core-2-Extreme/Video_player_for_3DS). De oorspronkelijke LICENSE, README_UPSTREAM.md, library-licenties en credits zijn behouden. Nieuwe portcode valt onder GPL-3.0-or-later. De oorspronkelijke decoderbronpakketten en buildinstructies zijn gekoppeld vanuit README_UPSTREAM.md/library; verspreid een gebouwde fork met de bijbehorende broncode en licentie-informatie. Er zijn geen films meegeleverd.
