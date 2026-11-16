Supplemento layout Sezione — 16/11/2026

Difetto preesistente distinto, non regressione della correzione UIcolori.
Riprodotto nel QDialog realmente visibile al suo default, senza resize del
pannello. Driver proprio: QAction Vista in sezione, combo index3. Il solo
PdfWindow principale ha dimensione1000x760, come scenario UI ordinario.

Baseline e candidato congelato producono JSON di geometria IDENTICI:
- prima del testo: panel257x271, label vuoto alto17;
- dopo Scelto nella vista: panel ancora257x271, sizeHint257x288;
- label alto24, heightForWidth34 e boundingRect del font alto34: deficit10;
- hide+riapertura da Parametri della sezione...: panel257x288, label34,
  heightForWidth34. Non usati adjustSize, resize o altri correttivi nel driver.

La cattura del tester274x281 non e necessaria per produrre il difetto: questo
si manifesta anche con font/layout che danno257x271. Non e soltanto una grab
di widget nascosto; panelVisible=true in tutti e tre gli stati registrati.
Il testo esteso nasce in gui.cpp1897, combo callback1914, show al toggle1919 e
riapertura1935. Il cambiamento del sizeHint non fa crescere subito la finestra.
Questa ultima relazione e riscontrata dalle geometrie, senza attribuire una
regola generale a tutti gli stili Qt. Nessuna nuova certificazione nativa.

Nessuna repoedit o correzione applicata. Il pacchetto final-proof precedente
e il suo sigillo restano invariati. Solo fonte inerte,ricette,log,JSON e sigillo;
PNG del widget restano nelle posizioni temporanee indicate da finding.json,
con hash e dimensioni. Nessun ELF/oggetto/libreria/CAD incluso qui.
