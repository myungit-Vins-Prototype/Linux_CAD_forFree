Nota audit — timeout finale generale tema/schede Wayland

Stato da registrare: NON PASS. Due run --theme-tabs terminano per timeout
exit124 dopo90.0138 e120.0114s. Log finali vuoti, nessun FAIL emesso; il metodo
stampa PASS soltanto alla fine, quindi il silenzio non prova completamento.

Evidenza ambiente: sessione3, Type=wayland, Active=yes, State=active,
LockedHint=yes. Schermo bloccato osservato, compatibile con un'attesa grafica;
nessuna causalita dimostrata. Al controllo host PID757192 gia inesistente:
nessun wchan/syscall/stack disponibile. Log gdb precedente del coordinatore
mostra ptrace negato (yama1); nessun attach o cambio permessi da questo audit.

42PNG: light0,dark1,light2 hanno12ciascuno; dark3 ha solo6, model0/model1.
Ordine pages: model0,model1,drawing0,drawing1. Ultima pagina salvata
 dark3-model1; i PNG dark3-drawing0 non sono stati raggiunti. Rettifica della
prima ipotesi: NON e provato che dark3 sia finita o che si stia creando una
finestra detached. Possibili punti sono attivazione/event processing/grab/menu
di drawing0, ma l'assenza dello stack impedisce di localizzarne uno.

Confronto indipendente: intero vecchio header046685e(18772byte) e prefisso
byte-identico del file corrente, SHA5ee295338bd353e9333a6331c44f6a0b285ffbea3f550da743375f73eb2a4da6.
La nuova regressione UI e aggiunta dopo il metodo preesistente. Le API rimosse
non avevano consumer; la correzione Sezione/massa non introduce attese Wayland.
Nessun indizio diretto di passaggio nel nuovo helper; questo NON esclude ogni
interazione senza stack e NON certifica come innocua qualunque futura modifica.

Separare risultati: app build e12CTest offscreen PASS; --theme-ui-residuals
Wayland74PASS exit0 in0.9653s; suite generale mista Wayland finale timeout.
Nessun nuovo run o modifica software suggerita per aggirare l'ambiente.
Un'eventuale nuova esecuzione con sessione sbloccata e lavoro successivo,
non requisito o test gia attribuito a questa diagnosi.

Sole letture; nessuna nuova finestra, nessun CAD utente, nessuna repoedit.
Metadata contiene comandi/osservazioni, exitcode, hash log e42PNG esistenti;
non duplica immagini o dati utente. Sigilli precedenti invariati.
