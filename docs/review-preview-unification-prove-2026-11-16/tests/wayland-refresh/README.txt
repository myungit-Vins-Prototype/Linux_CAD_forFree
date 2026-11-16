Diagnosi del replay Wayland e correzione del solo fixture — 16 novembre 2026

Esito
Oggetti APP Release correnti, senza overlay privati: 115 PASS / 0 FAIL offscreen;
115 PASS / 0 FAIL in tre esecuzioni Wayland consecutive. In ogni esecuzione
11 pannelli realmente visibili e 11 annullamenti con pulizia completa.
Lo stesso binario e' stato riutilizzato per tutte le quattro prove.
Fonte finale SHA256:
5b273897ba9bf229c73af586db0f133471768148cfca64c5e147b00854a7504c.
Binario finale SHA256:
abe0ed50e8aec81e0449870d34475c92af159cd64770f34d0276c506e2621ba8.
link-app-release.json ha private_overlay_objects vuoto. Nessun sorgente di
produzione modificato; nessuna rigenerazione del grafo. Gli oggetti APP sono
quelli della build completa root, con il solo main sostituito dal probe.

Causa dimostrata
Il fixture storico apre pannelli come finestre standalone: il suo QMainWindow
non contiene il CadViewport. Nel timer iniziale ridimensiona a 430x280 quando
la finestra nativa non e' ancora esposta. Nella sessione Wayland/niri corrente
il configure iniziale puo' sostituire questa misura con 1274x1401. Per cinque
form del replay iniziale questo elimina l'overflow (scroll maximum=0), dunque
l'asserzione di scorrimento fallisce correttamente per un prerequisito assente.
Le gerarchie e i footer sono corretti in tutti i casi; la posizione del footer
non cambia durante la chiamata di scroll. Non e' dimostrato un difetto del
footer di produzione.

Misure diagnostiche conservate
Pattern prima della resize: pannello 507x522, exposed=0.
Resize immediata: 430x280, exposed=0.
Dopo processEvents: pannello e finestra nativa 1274x1401, exposed=1,
scroll viewport 1234x1182, maximum=0, footer y=1299.
Dopo 20 e 100 ms la misura resta 1274x1401. Una seconda resize produce
430x280, maximum=1147 e footer y=178 immobile durante lo scroll.
La copia diagnostica completa passa 115/115 ma non e' la soluzione finale.
Attendere solo isExposed non risolve: 107 PASS / 8 FAIL.
QGuiApplication::sync prima/dopo resize non risolve: 107 PASS / 8 FAIL.
setFixedSize sulla finestra non risolve: 108 PASS / 7 FAIL.
Questi tentativi e relativi sorgenti/log restano separati.

Correzione minima del fixture
Il test limita a 120 px il vero QScrollArea del form, garantendo overflow
anche quando il compositor impone una finestra alta. Attende inoltre in modo
condizionato l'esposizione reale (deadline 2 secondi, eventi ogni 5 ms), e include
isExposed nella precondizione gia' verificata dall'asserzione di scroll.
Non viene introdotta una pausa fissa come criterio di successo.
Restano invariati e obbligatori: maximum positivo, scrollbar al massimo,
movimento effettivo del contenuto, identica posizione del footer, parent fuori
dallo scroll, ordine nel layout, unicita' dopo hide/show. Restano tutti i
precedenti test di stato e lifecycle. Non cambiano tolleranze o assert attesi.
Nel replay finale Wayland anche pannelli 1274x1401 hanno scroll_height=120,
maximum positivo e footer y=1299 invariato: la prova esercita lo scroll reale.

Controllo negativo con lo stesso fixture corretto
Sostituito soltanto common con l'oggetto privato della variante lazy precedente
all'helper footer (fonte common SHA e6e5a88d25dcc8e70b9bc3e7cba65cdb90a38cbe7d11dc7b9abc989fcc2d70fd).
Wayland: 79 PASS / 36 FAIL, 11/11 pannelli e annullamenti puliti. Falliscono
tutti i quattro controlli footer per ciascuno dei nove runner con PreviewScope.
Esempio Pattern: maximum=1134, messaggio da y=1312 a y=178 dopo lo scroll.
Dimostra che il nuovo fixture non fa passare automaticamente il test.
L'overlay del controllo negativo e' dichiarato in link-negative.json e NON
fa parte delle prove finali APP senza overlay.

Preservazione
Il probe storico tests/regressions.cpp.txt e i suoi manifest non sono cambiati.
Le prime prove root initial-replay-* rimangono separate. Il nuovo archivio e'
una sottocartella distinta; final-result-with-negative.json registra gli hash
delle fonti di produzione e dei risultati iniziali conservati. Nessun binario
o oggetto entra nell'archivio. CLAUDE.md e' stato letto prima dell'archiviazione;
il suo aggiornamento e la sigillatura dei manifest root restano al coordinatore.

Riproduzione finale
python3 build_regressions.py.txt --app-release --build-only
QT_QPA_PLATFORM=wayland QT_QPA_PLATFORMTHEME= QT_QPA_PLATFORMTHEME_QT6= QT_STYLE_OVERRIDE=Fusion /tmp/review-previews-wayland/regressions-app-release
Per offscreen sostituire QT_QPA_PLATFORM=offscreen.
Lo script usa i comandi di compilazione/link APP correnti. Se la fonte in /tmp
manca, usa regressions.cpp.txt accanto allo script archiviato. Il controllo
negativo usa build_negative.py.txt e richiede il vecchio oggetto privato indicato;
la fonte corrispondente e' conservata nell'archivio tests precedente come
common-before-footer.cpp.txt. I tentativi intermedi vuoti in sandbox non hanno
avviato una sessione Wayland; i replay riportati sono stati eseguiti nella
sessione grafica accessibile tramite l'escalation approvata.
