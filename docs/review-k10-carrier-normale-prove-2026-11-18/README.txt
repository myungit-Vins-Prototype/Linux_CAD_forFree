Prove locali K10: carrier a quota costante nonzero, 18 novembre 2026.

Prima della correzione, il wrap pubblico del box rialzato perde il rilievo
interno: residuo 1e-4 mm contro 1,22e-10 mm dichiarati. Baseline pubblica 2/3
gruppi; finale 18/18 Release e ASan/UBSan, app completa e 7/7 CTest PASS.
La normale naturale, il verso e la regolarita al cutoff esistente entrano nel
bound continuo; il solido errato e rifiutato conservativamente. Non e una
ricostruzione dell'offset generale. Famiglie e limiti nel rapporto allegato.

Sanitizer su sette TU; il resto del kernel e Release. Leak detection attiva,
non una verifica LSan dell'intera app. Nessun test UI. Fonte/binari/grafo e
43 pacchetti precedenti sono collegati dalla verifica finale. Nuovo lavoro
locale senza commit/push: la CI precedente non certifica questa patch.

Testo inerte, nessun ELF, libreria o CAD. Il replay richiede toolchain e build
indicate: non e ermetico. Temporanei binari rimossi come in cleanup.json.
Le copie before documentano la baseline, non il software corrente.
