# Certificati geometrici e limiti — audit indipendente

## Ellisse originale del cap rispetto al cilindro

Le coordinate dei getter originali sono convertite in Fraction dei corrispondenti binary64, confermati contro il dump hex del core. L'asse Y dell'ellisse è parallelo all'asse del cilindro. Indicati con c la proiezione radiale del centro e con a quella del semiasse X, vale esattamente rho(t)^2-R^2=A*cos(t)^2+2*B*cos(t)+C, con A=a·a>0, B=c·a e C=c·c-R². La convessità rispetto a cos(t) limita il massimo continuo ai due estremi ±1. Le radici quadrate sono racchiuse mediante isqrt su razionali e confronto intero esatto.

Residui radiali strettamente positivi ai due estremi: 1,64960410962e-12 e1,97910812234e-11 mm. Il massimo continuo dell'intero bordo ellittico è sotto2e-11 mm. Per i soli due microtratti E2/E5 conservati nella trace, π è racchiuso dalla formula di Machin con serie arctan alternate; cos(t)>=1-t²/2 limita il tratto ridotto. La distanza radiale continua è in[-1,7202542474e-14,+1,9791081224e-11] mm, dunque sotto2e-11 mm in valore assoluto. Nessun campione o provider di produzione partecipa a questa prova.

La prova riguarda i coefficienti analitici originali: non è una certificazione globale dell'intera superficie laterale del loft e non confronta un output booleana finale non esportato. I log del core dimostrano separatamente che snapshot e rigenerazione coincidono bytewise.

## Segmento finito e catena originale

Il certificatore privato revisionato usa il limite di interpolazione Peano M*DeltaT²/8 per ogni tratto ellittico, aggiunto al massimo dei gap fra estremi corrispondenti e retta finita. M è limitato dalla norma di Frobenius dei due semiassi originali A=rx*X e B=ry*Y; non presuppone assi matematicamente ortonormali. La derivata proiettata sulla direzione della retta è racchiusa al punto medio e ampliata di M*reach*||lineDirection||, con reach calcolato outward. Positività stretta prova monotonia; dati inconclusivi non autorizzano il percorso.

Per i piani si limita la distanza al piano originale tramite il prodotto misto e norma del cross dei suoi assi. Per il cilindro una base ideale ortonormale sullo stesso asse viene comparata al frame originale con erroreR*(||X-Xideal||+||Y-Yideal||). La quadratica di distanza radiale della retta finita è racchiusa dai tre coefficienti Bernstein; |rho-R|<=|rho²-R²|/R. La composizione con il gap Peano resta nello stesso budget fissato prima della costruzione.

## Immagine UV affine dei soli nuovi usi

Il contributo privato affinePCurveError calcola E(t)-S(affineUV(t)) con coefficienti originali, non valutazioni campionate. Il bound è max(gap agli estremi)+(Mellipse+Mcomposition)*DeltaT²/8. Piano: Mcomposition=0. Cilindro: Mcomposition*DeltaT² <= R*(||X||+||Y||)*DeltaU²; la forma diretta in DeltaU evita divisioni per span minuscoli. Le coordinate UV provengono da Line2 o B-spline2 grado1/duepoli/pesi1/nodi clamped. La trigonometria usa Taylor razionale intervallare e la proprietà1-Lipschitz per argomenti UV intervallari.

La funzione è pura e non altera le pcurve sorgenti. Ogni overflow, dato non finito, famiglia non supportata o intervallo non risolto restituisce infinito. Il core applica il bound positivo ai soli nuovi usi e ricertifica l'intervallo effettivo dopo unify; il certificatore da solo non dimostra la corrispondenza genealogica, verificata separatamente nel montaggio tramite identità fin/edge/loop/vertex.

## Distinzione delle prove

La prova Fraction originale è indipendente dal provider. L'estensione affine UV è un contributo implementativo di questo agente: non viene presentata come una seconda validazione indipendente di sé stessa. Il test agent esegue le regressioni runtime separate. Questo agente ha verificato la sintassi C++17 con Wall/Wextra/Wpedantic e revisionato i sorgenti privati congelati; non ha compilato l'app né modificato produzione.
