# WanagoBack voor New Nintendo 3DS — prototype 0.1

Dit is een BRONPROJECT met GitHub Actions-build, geen reeds gebouwde of op hardware geteste CIA. De workflow is toegevoegd maar is tijdens het maken van dit pakket niet op GitHub uitgevoerd. Er kunnen nog compiler- of consoleproblemen blijken uit de eerste build/test.

## Wat deze eerste port bevat

- Native geel/blauwe WanagoBack-bibliotheek, bovenste scherm voor titel/status, onderste scherm voor de titellijst.
- Haalt je bestaande https://raw.githubusercontent.com/WanagoBack/testing/main/library.json op; bewaart een kopie op SD voor offline gebruik. Maximaal 512 catalogusposities en 1 MiB JSON.
- D-pad, A en touchscreen om te kiezen; X om te verversen, Y om de gewone SD-videoplayer te openen, START om vanuit de bibliotheek af te sluiten.
- Speelt voorbereide SD-video's met de decoder van Video player for 3DS. Hardwaredecodering staat upstream standaard aan op New-modellen.
- Optioneel `url3ds`: een directe HTTPS-link naar een geschikte MP4. Die wordt eerst volledig naar SD gedownload. Daarna druk je A om te kijken. B annuleert een download. Er is nog geen gedeeltelijke downloadhervatting of directe streaming.
- Afzonderlijke app-ID 0xEC610. Dit is een gekozen homebrew-ID, geen officieel gereserveerde registratie. De upstream zelf-updater staat uit.

## Wat nog NIET geport is

MEGA-decryptie/direct MEGA-streamen, automatisch gegenereerde thumbnails, pc-profielen, games, boeken, de volledige oorspronkelijke interface en alle pc-animaties. Bestaande `megaUrl`-items staan wel in de bibliotheek en worden aan voorbereide lokale bestanden gekoppeld. Je hoeft je bestaande online JSON hiervoor niet aan te passen. Dit project lost de Windows-startfout niet op.

## CIA laten bouwen op GitHub

1. Pak deze ZIP uit.
2. Maak met GitHub Desktop een nieuwe repository en zet de INHOUD van deze projectmap in de repositorymap. `Makefile`, `source`, `library`, `tools` en `.github` moeten direct in de root staan. De verborgen `.github`-map moet mee; upload niet alleen de ZIP.
3. Commit en publiceer de repository naar je GitHub-account. Er is niets vanuit deze sessie gepubliceerd.
4. Open op GitHub **Actions → Build WanagoBack New 3DS CIA**. De push naar main/master start de build; anders kies **Run workflow**.
5. Bij een groene build download je onder **Artifacts** het pakket `WanagoBack-New3DS-test-build`. Daarin staan `WanagoBack.cia` en `WanagoBack.3dsx`.
6. Installeer de CIA met FBI op een New 3DS/New 3DS XL/New 2DS XL met homebrew-ondersteuning. Bewaar de volledige buildlog als de workflow rood wordt; er wordt dan geen succesvolle CIA geclaimd.

De workflow gebruikt de officiële devkitPro devkitarm-container en vastgelegde makerom/bannertool-versies. De compiler in het containerlabel `latest` kan veranderen; de werkelijke compilerversie wordt meegeleverd in BUILD_VERSIONS.txt. Het upstream project bevat vooraf gebouwde decoderbibliotheken voor devkitARM r68. Voor een andere ABI kunnen die opnieuw gebouwd moeten worden volgens library/_devkitpro_install.md.

## Video's voorbereiden — zonder thuisserver

Download de gewenste video's zelf en zet ze in één map. Geef iedere video de titel uit `library.json` als bestandsnaam, bijvoorbeeld `Klus op dieet.mp4`. Python 3 en FFmpeg moeten op de computer geïnstalleerd zijn.

Voorbeeld op macOS (pas je mapnamen aan):

```sh
python3 tools/prepare_sd.py --videos "$HOME/Movies/WanagoBack" --sd "$HOME/Desktop/WanagoBack-SD"
```

Kopieer daarna de gemaakte map `3ds` naar de root van je 3DS-SD-kaart. De computer hoeft tijdens het kijken niet aan te staan. De tool maakt H.264 baseline/AAC-video's van 400×240 met behoud van beeldverhouding. Bestaande voorbereide bestanden worden overgeslagen; `--overwrite` vervangt ze. Met `--library pad/naar/library.json` kun je ook zonder internet voorbereiden.

Geen lokale video gevonden? De tool meldt de ontbrekende titels. Meerdere lokale bestanden met dezelfde titel worden gemeld en overgeslagen. De tool downloadt zelf niets van MEGA en omzeilt geen hostinglimieten.

## Optionele rechtstreekse download

Voeg per video een `url3ds` toe naast bestaande velden:

```json
{"videos":[{"folder":"Plop","title":"Voorbeeld","url3ds":"https://jouw-host.example/video-400x240.mp4"}]}
```

Gebruik een daadwerkelijke videolink, geen webpagina/deellink. Alleen HTTPS met certificaatcontrole is toegestaan. Downloaden vereist voldoende vrije SD-ruimte voor het hele bestand. De app doet geen omzetting op de console; gebruik vooraf een geschikt H.264/AAC-bestand.

## Validatie en resterende test

Gecontroleerd bij oplevering: Python-syntaxis, conversie van een gegenereerde testvideo, cachebestandsnamen en aanwezigheid van buildinputs. De nieuwe C-frontend is op de host op syntaxis gecontroleerd met platformstubs; dat is geen ARM-linktest. De workflow controleert na compilatie de ARM ELF-identiteit, 3DSX-header en CIA-sectiegrenzen. Geen van die controles bewijst dat audio/video, touchscreen of installatie op hardware al werkt.

Test na de eerste geslaagde build: opstarten, catalogus laden, offline opstart met SD-catalogus, één voorbereide video afspelen met geluid, teruggaan naar bibliotheek, download annuleren en opnieuw starten. Begin met een korte video.

## Herkomst en licenties

Gebaseerd op Core-2-Extreme/Video_player_for_3DS, commit 3b326d272344087017549f5f1ac5eadf06e413b7 (https://github.com/Core-2-Extreme/Video_player_for_3DS). De oorspronkelijke LICENSE, README_UPSTREAM.md, library-licenties en credits zijn behouden. Nieuwe portcode valt onder GPL-3.0-or-later. De oorspronkelijke decoderbronpakketten en buildinstructies zijn gekoppeld vanuit README_UPSTREAM.md/library; verspreid een gebouwde fork met de bijbehorende broncode en licentie-informatie. Er zijn geen films meegeleverd.
