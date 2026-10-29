#!/usr/bin/env python3
"""Catalogo verificabile + dipendenze include + chiamate C++ risolte da libclang.

Generazione: python3 tools/generate_architecture.py --build-dir build-linux
Verifica senza libclang: python3 tools/generate_architecture.py --check
Il build deve avere CMAKE_EXPORT_COMPILE_COMMANDS=ON. Non esegue i sorgenti.
"""
from __future__ import annotations
import argparse
import concurrent.futures
import hashlib
import json
import os
import posixpath
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile
from contextlib import contextmanager

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'docs/architecture'
VERSION = 1
GROUPS = [
 ('startup', 'Avvio e finestra', 'Ingresso applicazione, menu, toolbar, azioni e coordinamento della finestra.'),
 ('viewport', 'Viewport e interazione', 'Stato GUI della vista 3D, input, selezione, sketch e orchestrazione delle anteprime.'),
 ('render', 'Rendering e camera', 'Camera, cache grafiche, passate OpenGL, trasparenza e picking GPU.'),
 ('dialogs', 'Dialoghi e storyboard', 'Parametri dei comandi, dialoghi per dominio e albero della storia.'),
 ('sketch', 'Sketch e vincoli', 'Geometria 2D/3D dello sketch, editing, snap, riferimenti e risoluzione dei vincoli.'),
 ('features', 'Feature e ricostruzione', 'Operazioni parametriche, dipendenze, ricostruzione e calcolo delle anteprime.'),
 ('documents', 'Documenti e revisioni', 'Persistenza, accesso, revisioni, recupero e metadati del modello.'),
 ('drawing', 'Tavole 2D', 'Viste, sezioni, quote, presentazione ed esportazione delle tavole.'),
 ('io', 'Importazione ed esportazione', 'Adattatori dei formati e integrazione con librerie esterne.'),
 ('analysis', 'Analisi e mesh', 'Analisi geometrica, discretizzazione e supporto al calcolo.'),
 ('kernel', 'Kernel geometrico', 'Matematica, curve, superfici, topologia B-rep e operazioni geometriche di base.'),
 ('model', 'Modello e servizi comuni', 'Tipi condivisi, utilità e servizi applicativi trasversali.'),
 ('tests', 'Test e benchmark', 'Verifiche automatiche e misure; non fanno parte dell’eseguibile distribuito.'),
 ('build', 'Build e strumenti', 'Configurazione, generazione e confezionamento del software.')]
# Descrizioni curate: aggiornare queste voci quando cambia il confine di un modulo.
DESCRIPTIONS = {
 'cad_surface_tools': ('features', 'Estensione fino a riferimenti multipli: raggruppamento delle facce e cucitura del target; rete U/V e poli NURBS, storia e codec opzionale dei target nel formato53.'),
 'fk_surface_tools': ('kernel', 'Reti a celle Coons cucite, prolungamento e selezione dai pezzi gia intersecati senza secondo split, modifica relativa di poli/pesi con ricostruzione dei bordi e trim UV conservati.'),
 'forgeCad2026_dialogs_surface_tools.cpp': ('dialogs', 'Pannelli superfici: bordo libero iniziale, selezione continua di più target con lista, toggle/rimozione e termine per anteprima; famiglie U/V; poli multipli con gizmo globale/locale e anteprima al rilascio.'),
 'kernel/tests/test_surface_tools.cpp': ('tests', 'Reti non planari, curve invertite e incroci invalidi; estensione a piani/facce curve, target spezzati e tra estrusioni ortogonali da spline; poli/pesi, trim/fori, topologia e input degeneri.'),
 'kernel/tests/surface_tools_fixture.h': ('tests', 'Fixture condivisa NURBS bicubica con trim UV e foro per prove geometriche e native delle superfici.'),
 'tests/test_surface_tools_gui.h': ('tests', 'Funzioni superficie: target multipli; poli in gruppo, assi globali/locali, Esc e anteprima al rilascio; icone distinte, dipendenze, cache, persistenza, revisioni e readonly.'),
 'fk_fill': ('kernel', 'Riempimenti con contorni/guide, contatti G0/G1/G2 per faccia, influenza locale e guida direzionale della forma; rapporti numerici e controllo di regolarita.'),

 'tests/test_drawing_break.cpp': ('tests', 'Viste interrotte: coordinate X/Y, quote originali, ritaglio, codec, dialogo, Undo, modello e PDF.'),
 'tests/test_projected_drawing_file.h': ('tests', 'File nativi e revisioni delle tavole proiettate, ausiliarie e interrotte; protezione dello storico e PDF.'),
 'tests/test_sketch_slot_point.h': ('tests', 'Asola rettilinea e punto autonomo: mouse, geometria esatta, volume, vincoli, Undo/Redo, formato52 e comandi UI.'),
 'cad_curve_solver': ('sketch', 'Geometria esatta e campionamento delle curve; scomposizione asola in segmenti/archi e punti autonomi senza profilo.'),
 'fk_blend_surface': ('kernel', 'Raccordi e smussi a palla rotolante: sezioni normali e guida intrinseca per coperchi piani su fianchi B-spline tangenti, continuazione delle cuciture tangenti, chiusure terminali e controllo dei contorni.'),
 'fk_blend': ('kernel', 'Raccordi e smussi analitici: sezioni, utensili sottrattivi/additivi con copertura degli appoggi curvi anche singoli, catene tangenti, pezze terminali e genealogia.'),
 'kernel/tests/test_blend.cpp': ('tests', 'Regressioni geometriche dei raccordi: volumi e tangenze, assenza di anelli residui su appoggi sferici singoli, cuciture, supporti consumati, coperchi piani e catene aperte su cuciture tangenti elicoidali, con controlli di raggio, topologia e persistenza.'),
 'forgeCad2026.cpp': ('startup', 'Entry point Qt: avvio, opzioni e apertura della finestra principale.'),
 'forgeCad2026_application_theme': ('startup', 'Stile Fusion comune ripristinato dopo sostituzioni native, palette chiaro/scuro e diagnostica dei cambi tema.'),
 'forgeCad2026_gui': ('startup', 'PdfWindow: sette fasi di inizializzazione di scena, menu, comandi, opzioni e toolbar; ciclo del documento e apertura riutilizzabile dei modelli archiviati in sola lettura.'),
 'forgeCad2026_gui_common': ('startup', 'Helper condivisi dalla finestra, dai dialoghi e dal viewport.'),
 'forgeCad2026_viewport.cpp': ('viewport', 'Ciclo di vita e impostazioni pubbliche del coordinatore CadViewport; gabbia poli multipli, proiezione/picking del gizmo e delta vincolati agli assi 3D.'),
 'forgeCad2026_viewport.h': ('viewport', 'Contratto e stato privato del coordinatore GUI della vista 3D.'),
 'forgeCad2026_viewport_internal.h': ('viewport', 'Dipendenze di implementazione e template condivisi dai soli file viewport.'),
 'forgeCad2026_viewport_document.cpp': ('viewport', 'Soppressione: invalida il filtro dei corpi rilasciati prima della normalizzazione; Documento e storia unificata: risultati delle ripetizioni contigui alla madre, rimappatura dei riferimenti, schizzi e piani autonomi e appoggi associativi, contesto storico per i riferimenti e risultati confermati visibili nella sola sessione schizzo corrente.'),
 'forgeCad2026_viewport_interaction.cpp': ('viewport', 'Eventi Qt di mouse, tastiera, ruota e menu contestuali.'),
 'forgeCad2026_viewport_selection.cpp': ('viewport', 'Filtri di selezione, hover, riferimenti, hit testing e risoluzione della funzione creatrice della faccia scelta.'),
 'forgeCad2026_viewport_sketch.cpp': ('viewport', 'Comandi schizzo, asola rettilinea con vincoli e punti autonomi: creazione, modifica, quote e riferimenti.'),
 'forgeCad2026_viewport_preview.cpp': ('viewport', 'Orchestrazione asincrona delle anteprime: generazioni, cancellazione e consegna sul thread GUI.'),
 'forgeCad2026_viewport_render.cpp': ('viewport', 'Frame e overlay con gabbia NURBS, poli selezionati e gizmo colorato globale/locale; visibilita storica in schizzo, anteprime feature anche nello schizzo con vista sbloccata e prepass di profondita per trasparenza a singolo strato.'),
 'tools/diagnose_menus.sh': ('build', 'Avvio diagnostico dei popup: log tema/attributi e immagini per strati, con protocollo Wayland opzionale.'),
 'tools/run_ribbon_preview.sh': ('build', 'Avvio esplicito della prova ribbon separata, senza sostituire l’app abituale.'),
 'forgeCad2026_ribbon_preview': ('startup', 'Ribbon predefinita del viewport: schede/gruppi adattivi, azioni condivise con pulizia alla distruzione, configurazione persistente e contesto.'),
 'forgeCad2026_ribbon_editor': ('dialogs', 'Editor modale della ribbon: sezioni, comandi, separatori e ripristino su copia confermabile.'),
 'forgeCad2026_ribbon_preview_main.cpp': ('startup', 'Avvio separato della prova ribbon, preferenze isolate, cattura e regressione apertura documento/Blocco.'),
 'forgeCad2026_zoom': ('viewport', 'Normalizzazione input per backend, preferenze condivise e animazione temporale dello zoom per vista 3D e tavole.'),
 'forgeCad2026_viewport_scene.cpp': ('viewport', 'Coordinate scena/schermo, raggi, bounds, zoom uniforme al cursore, adattamento vista e piani di riferimento.'),
 'forgeCad2026_feature_preview': ('features', 'FeaturePreviewBuilder: chiave e calcolo delle anteprime da snapshot, senza possedere widget.'),
 'forgeCad2026_sketch_curvature': ('sketch', 'SketchCurvatureCache: possiede e invalida i pettini di curvatura al cambio di curva o scala.'),
 'forgeCad2026_camera': ('render', 'Stato e trasformazioni della camera 3D.'),
 'forgeCad2026_scene_renderer': ('render', 'Passate grafiche della scena, FBO/MSAA e disegno di corpi, riferimenti e overlay.'),
 'forgeCad2026_gpu_picker': ('render', 'Picking tramite identificatori GPU e gestione dei buffer necessari.'),
 'forgeCad2026_glass_compositor': ('render', 'Composizione della trasparenza dei corpi e risorse OpenGL associate.'),
 'forgeCad2026_storyboard': ('dialogs', 'Albero della storia: presentazione, selezione, riordino e segnali delle feature; modalita di sola scelta della sorgente per le ripetizioni.'),
 'forgeCad2026_dialogs_solids.cpp': ('dialogs', 'Dialoghi solidi: ripetizioni con un solo blocco della finestra, storyboard selezionabile nel pannello incorporato e tre passaggi visibili (sorgente, riferimento, parametri); funzioni storiche con bersaglio successivo e sformo a piano neutro.'),
 'forgeCad2026_dialogs_surfaces.cpp': ('dialogs', 'Dialoghi di parametri per operazioni sulle superfici.'),
 'forgeCad2026_dialogs_sketch.cpp': ('dialogs', 'Dialoghi di creazione e modifica degli sketch.'),
 'forgeCad2026_dialogs_analysis.cpp': ('dialogs', 'Dialoghi per analisi e opzioni di verifica geometrica.'),
 'forgeCad2026_dialogs_common': ('dialogs', 'Pannelli incorporati: messaggi completi con scrolling H/V, pulsanti accessibili e dimensioni persistenti per identita semantica della funzione; palette e vetro sincronizzati.'),
 'forgeCad2026_dialogs.h': ('dialogs', 'Contratti dei dialoghi di comando per solidi, superfici, sketch e analisi.'),
 'toolbar_editor.h': ('startup', 'Editor della disposizione e dei comandi delle toolbar.'),
}

# Catalogo dei moduli applicativi: descrizioni specifiche condivise da header e sorgente.
DESCRIPTIONS.update({
 'tests/test_drawing_scale.cpp': ('tests', 'Scale del foglio e delle viste contro dimensioni analitiche: catene di ereditarieta, personalizzazioni, quote, codec legacy/schema23 e badge, Undo, sola lettura e dialoghi reali con anteprime.'),
 'kernel/fk_boolean.cpp': ('kernel', 'Booleane esatte: certificati di appartenenza e filtro dei candidati entro tolleranza prima di proiettare gli spigoli; verifica originale e topologia conservate.'),
 'kernel/tests/test_mass.cpp': ('tests', 'Massa e area contro formule/OCCT; risultati seriali/paralleli bit identici, input immutato, richieste concorrenti, primo errore e cancellazione.'),
 'kernel/tests/test_curve_surface.cpp': ('tests', 'Intersezioni confrontate con OCCT e conservazione della geometria nelle suddivisioni Bezier: razionali, micro domini, gradi alti e divisioni ripetute.'),
 'kernel/tests/test_curve_algo.cpp': ('tests', 'Proiezioni contro OCCT, filtro esaustivo e linea analitica4096tratti: range inclusivi/ristretti, estremi e concorrenza; quadratura e cache delle lunghezze.'),
 'kernel/tests/test_bspline_surface.cpp': ('tests', 'Superfici B-spline contro OCCT e Bernstein indipendente fino al grado31; limiti dei buffer, nodi ripetuti/stretti, prefissi bit identici, concorrenza e cancellazione.'),
 'kernel/tests/test_boolean.cpp': ('tests', 'Booleane contro OCCT, contatti e sweep elicoidali; volumi analitici delle lamelle spline sottili fino a 1e-4 mm, topologia e concordanza parallela.'),
 'kernel/fk_surface_distance.h': ('kernel', 'Contratti dei filtri di distanza: candidato entro tolleranza o rigetto conservativo, senza usare il filtro come accettazione geometrica.'),
 'kernel/fk_surface_distance.cpp': ('kernel', 'Filtri conservativi di distanza: certificato entro soglia e rigetto tramite BVH dei box Bezier con margine numerico; proiezione globale nei casi incerti.'),
 'kernel/fk_mass.cpp': ('kernel', 'Proprieta di massa esatte: quadrature indipendenti per faccia in parallelo, somme nello stesso ordine seriale, errori ordinati e cancellazione; worker annidati e corpi piccoli restano locali.'),
 'kernel/fk_loft.cpp': ('kernel', 'Loft esatto con cache locale limitata degli integrali di lunghezza: stessi tratti, tolleranze e ordine di somma, nessuna approssimazione della parametrizzazione.'),
 'kernel/fk_intersect.cpp': ('kernel', 'Intersezioni esatte: gerarchia locale dei box Bezier2D con visita in ordine di parametro, scarto conservativo delle coppie separate; Newton e tolleranze invariati.'),
 'kernel/fk_exchange.cpp': ('kernel', 'Modello di scambio e assemblaggio: cucitura, campioni UV locali e proiezioni esaustive entro distanza con scarto conservativo dei tratti lontani; verifica 3D e topologia conservate.'),
 'kernel/fk_curve_surface.cpp': ('kernel', 'Intersezioni curva-superficie e suddivisione Bezier diretta in coordinate omogenee: stesso ordine di inserimento nodi, ripiego generale per pezze non clamped, nessuna modifica delle tolleranze.'),
 'kernel/fk_curve_algo.cpp': ('kernel', 'Proiezioni esatte: limita i tratti Bezier al range con bounds inclusivi, conservando ordine/candidati e filtri conservativi entro distanza; lunghezze e quadratura invariate.'),
 'kernel/fk_bspline_surface.cpp': ('kernel', 'Superfici B-spline: percorso del solo punto senza buffer delle derivate, stessi termini e ordine di somma; derivate e cache delle pezze invariate.'),
 'kernel/fk_marching.cpp': ('kernel', 'Intersezioni superficie/superficie: riuso limitato delle proiezioni; filtro conservativo delle pezze NURBS prima di proiettare i vertici dei coni; rami singolari e tolleranze conservati.'),
 'kernel/tests/test_marching.cpp': ('tests', 'Intersezioni contro OCCT e geometrie analitiche: tangenze, poli e sella con solo termine misto, diagonali quasi nulle e ordine delle superfici invertito.'),
 'kernel/fk_bspline_basis.cpp': ('kernel', 'Basi e derivate B-spline: specializzazioni dei gradi1..5 e ordini0..2 con algoritmo e ordine aritmetico invariati, fallback generale e cancellazione per chiamata.'),
 'kernel/tests/test_bspline.cpp': ('tests', 'Valutazioni B-spline contro OCCT e formule Bernstein, prefissi derivati bit identici, gradi fino31, nodi ripetuti/limiti, buffer, concorrenza e cancellazione.'),
})

DESCRIPTIONS.update({'tools/open_architecture.py': ('build', 'Avvio locale della mappa: aggiorna i dati a ogni apertura e li mostra nel browser con avanzamento ed errori.'), 'CMakeLists.txt': ('build',
                    'Definisce eseguibile e suite; libreria interna condivisa dai runner delle tavole e impronta geometrica limitata alla chiusura delle dipendenze del calcolo.'),
 'cmake/geometry_sources.cmake': ('build', 'Raccoglie include locali transitivi e implementazioni per l’impronta geometrica; esclude moduli visuali indipendenti e conserva il kernel completo.'),
 'tests/test_geometry_sources.cmake': ('tests', 'Verifica invalidazione per dipendenze geometriche, stabilita dopo modifiche visuali e manifesto reale della build.'),
 'tests/test_drawing_multisheet.h': ('tests', 'Contenitore multifoglio: aggiornamento con un calcolo per vista, annullamento ed errore atomici, presentazioni e storia.'),
 'tests/test_document_units.h': ('tests', 'Isolamento delle unita fra due finestre, quote, misure, campi numerici, anteprime e ripristino; valori interni invariati.'),
 'tests/test_history_suppression.h': ('tests', 'Soppressione con appoggio/Pierce invalidi, continuita della catena e rifiuto alla riattivazione.'),
 'tests/test_function_panels.h': ('tests', 'Messaggi lunghi H/V copiabili, pulsanti accessibili, chiavi indipendenti, migrazione e riduzione temporanea del viewport.'),
 'fk_surface_algo': ('kernel', 'Algoritmi sulle superfici: coincidenza delle estrusioni certificata sull intero profilo razionale con coefficienti Bernstein proiettati.'),
 'fk_loft': ('kernel', 'Loft con contatti G1/G2: basi e derivate di collocazione in coordinate locali dei nodi, portabili su ARM anche per campate di un ulp.'),
 'kernel/tests/test_surface_algo.cpp': ('tests', 'Algoritmi di superficie e certificato coincidenza delle estrusioni: onde, NURBS, verso inverso e suddivisione dei nodi.'),
 'kernel/tests/test_loft.cpp': ('tests', 'Loft, contatto e degenerazioni; campate di un ulp con profili razionali/polinomiali in entrambi i versi.'),
 'cad_constraints': ('sketch', 'Definisce e applica vincoli geometrici e dimensionali degli sketch.'),
 'cad_cuda_tessellation': ('analysis', 'Adatta il calcolo di tessellazione ai percorsi accelerati CUDA.'),
 'cad_curve_extract': ('features',
                       'Estrae curve 3D associative dalla geometria sorgente e dai riferimenti della '
                       'feature.'),
 'cad_curve_solver': ('sketch',
                      'Costruisce e ricalcola le curve esatte a partire dalle entità dello sketch.'),
 'cad_datum': ('features', 'Risoluzione dei piani e dei riferimenti geometrici di costruzione.'),
 'cad_display_cache': ('render', 'Cache CPU/VBO dei display: riuso nel frame e rilascio delle risorse non usate a fine frame, statistiche di storage referenziato; facce, spigoli e istanze.'),
 'cad_memory': ('render', 'Rilascio best effort delle pagine libere glibc dopo grandi riduzioni delle mesh; nessun oggetto vivo o stato Undo modificato, no-op sugli altri allocatori.'),
 'tests/test_mesh_memory.h': ('tests', 'Sonda RAM bassa/massima/bassa su documenti: RSS, arene, storage mesh e cache GL, frame reali e trim solo diagnostico; nessun salvataggio CAD.'),
 'tests/test_display_cache_memory.cpp': ('tests', 'Durata cache CPU/VBO: rilascio nel primo frame sostitutivo o vuoto, riuso senza crescita, risorse condivise e variante nativa OpenGL.'),
 'cad_document_io': ('documents',
                     'Codec nativo 54: nomi topologici di appoggi e Pierce, lettura legacy 1–53 e confronto delle definizioni storiche senza perdita dei nuovi riferimenti.'),
 'cad_document_read': ('documents', 'Lettura del documento e opzioni di apertura/consultazione.'),
 'cad_document_recovery': ('documents',
                           'Checkpoint di recupero e ripristino dei documenti dopo interruzioni.'),
 'cad_document_version.h': ('documents',
                            'Identità e informazioni di versione dei documenti e dei check-in.'),
 'cad_drawing': ('drawing',
                 'Tipi e calcolo geometrico delle viste, incluse sezioni rimosse a gradini multipli e allineate a due piani angolari con contorni esatti e provenienza sui piani originali.'),
 'cad_drawing_annotations.h': ('drawing',
                               'Note, datum e cartigli geometrici con riferimenti associativi e propagazione degli orfani.'),
 'cad_drawing_numbering.h': ('drawing', 'Badge persistenti per UUID, blocchi contigui per vista e rotazione locale dalla prima quota; ordine delle viste e percorso spaziale interno in otto versi, filtri, riserve, scambio e confronto; glifi vettoriali.'),
 'tests/test_drawing_dimension_numbering.h': ('tests', 'Regressioni badge: viste contigue su coordinate reali di Controllo numero quote, prima interna/altre viste, sovrapposizioni, GUI con blocchi8+3, flacone, otto versi, filtri, riserve, codec, Undo e PDF.'),
 'cad_drawing_technical.h': ('drawing', 'Validazione e disegno vettoriale di datum e cartigli geometrici; riferimenti per UUID e testo derivato.'),
 'cad_drawing_axes.h': ('drawing', 'Convenzioni degli assi e conversioni delle coordinate delle viste 2D.'),
 'cad_drawing_browser': ('drawing', 'Navigazione e scelta di documenti/modelli per le tavole.'),
 'cad_drawing_centers.h': ('drawing',
                           'Calcolo e riconoscimento dei centri geometrici utilizzati nelle tavole.'),
 'cad_drawing_compute.h': ('drawing',
                           'Calcolo delle viste su worker con avanzamento, annullamento e blocco '
                           'temporaneo dell’editor.'),
 'cad_drawing_derived': ('drawing', 'Propagazione delle scale foglio/personalizzata/madre e costruzione/allineamento delle viste ortogonali, ausiliarie e in sezione; composizione del tratteggio delle sezioni a gradini multipli e angolari, recupero del materiale con classificatori esatti preparati e riutilizzati per faccia.'),
 'cad_drawing_break.h': ('drawing', 'Valida e ritaglia le viste interrotte, escludendo i percorsi fuori taglio senza discretizzarli; mappa la presentazione senza alterare curve e misure.'),
 'cad_drawing_detail.h': ('drawing', 'Contorni circolari o poligonali dei dettagli, ritaglio grafico senza nuovi riferimenti di quota e maschere delle immagini ombreggiate.'),
 'cad_drawing_dimension': ('drawing', 'Geometria delle quote 2D, generatrici coniche/cilindriche con estremi associativi, diametri riconosciuti dalla geometria, catene e ordinate con segno.'),
 'cad_drawing_editor': ('drawing',
                        'Editor della tavola: scala comune del foglio e sorgente della scala per ogni vista, aggiornamento atomico di anteprime e dipendenze; selezione, posizionamento e comandi; inserimento atomico e anteprime di catene e ordinate, proprietà e anteprime dei datum/cartigli geometrici e dei badge persistenti con scelta della prima quota e frecce nella viewport, incluse anteprime delle viste interrotte e sezioni rimosse a gradini multipli e allineate a due piani angolari.'),
 'cad_drawing_preparation': ('drawing', 'Prepara geometria, curve e visibilita dei fogli senza QWidget; risultati trasferibili all’editor per aggiornamento atomico multifoglio senza ricalcoli.'),
 'cad_geometry_refs.h': ('features', 'Visitatori const/mutabili dei riferimenti geometrici per famiglie; enumerazione condivisa da storia, dipendenze e topologia, distinta dal contratto binario del codec.'),
 'tests/test_geometry_refs.cpp': ('tests', 'Copertura delle famiglie dei riferimenti e delle mutazioni, inclusi support/Pierce e target multipli.'),
 'cad_drawing_file': ('drawing', 'Contenitore .drw mono/multifoglio, identita e riferimenti delle copie, consumo dei risultati degli aggregati storici; verifica impronte del modello con compatibilita53 limitata alle definizioni senza nuovi nomi support/Pierce.'),
 'cad_drawing_focus.h': ('drawing',
                         'Ripristina il focus alla finestra della tavola dopo i dialoghi nativi.'),
 'cad_drawing_page': ('drawing', 'Impaginazione, formati, scala del foglio nel cartiglio con rapporti 1:n, fogli numerati N/totale e revisioni.'),
 'cad_drawing_release': ('drawing', 'Ponte CAD dei rilasci coordinati: verifica payload nativi, identita, modello preciso e impronta delle tavole prima di pubblicare o leggere un manifesto.'),
 'cad_drawing_release_ui.cpp': ('drawing', 'Scelta delle revisioni compatibili, verifica/pubblicazione dei rilasci e consultazione con apertura storica del modello3D e delle tavole multipagina.'),
 'tests/test_drawing_release.h': ('tests', 'Rilasci nativi: modelli e tavole multipagina storiche, payload incoerenti, GUI di creazione/consultazione/apertura3D-2D, readonly e PDF.'),
 'tests/test_revision_releases.cpp': ('tests', 'Manifesti immutabili: versioni/hash, dipendenze, atomica/idempotenza, due processi concorrenti, corruzione e spostamento archivio.'),
 'cad_drawing_restore.cpp': ('drawing', 'Ripristino atomico dei fogli e del modello archiviato nella copia di lavoro, base e token correnti conservati e Undo/Redo con sorgente verificata.'),
 'tests/test_drawing_revision_restore.h': ('tests', 'Ripristino A dopo B: fogli e modelli diversi, numeri, base/token, annullamento atomico, Undo/Redo, GUI/PDF e nuovo discendente C.'),
 'cad_drawing_revision_ui.cpp': ('drawing', 'Comandi delle revisioni, selezione e ripristino nella copia di lavoro, confronto dei badge per UUID prima del check-in e con gli storici, pannello e riapertura con percorsi canonici.'),
 'cad_drawing_revisions': ('drawing', 'Integra le tavole con il protocollo di revisioni dei documenti.'),
 'cad_drawing_sheet': ('drawing',
                       'Stato e codec del foglio fino allo schema23, scala del foglio e scale delle viste con ereditarieta e compatibilita storica, assegnazioni persistenti dei numeri e compatibilita legacy, posizione e interruzione grafica delle viste, relazioni madri/derivate.'),
 'fk_sheet': ('kernel', 'Taglio e rifilo di lamine; selezione del pezzo piu vicino condivisa con estensione, senza ripetere le intersezioni.'),
 'fk_intersect': ('kernel', 'Intersezioni esatte curve/superfici; proiezione planare di curve trasformate con conservazione dei parametri; radici Bernstein con riuso dei coefficienti e filtro conservativo prima delle allocazioni per le pezze lontane.'),
 'kernel/tests/test_intersect.cpp': ('tests', 'Intersezioni curve/superfici e radici Bernstein; immagini planari di spline razionali, coniche e rette trasformate, trim annidati e parametro delle rette scalate.'),
 'fk_classify': ('kernel', 'Classificazione esatta faccia/solido; FaceClassifier prepara limiti parametrici, poli e box conservativi dei bordi per interrogazioni ripetute senza cambiare tolleranze.'),
 'tests/test_drawing_performance.h': ('tests', 'Benchmark esplicito in sola lettura di tavole reali: tempi modello, curve, contorni, campiture, visibilita e apertura editor; impronte SHA256 dei tracciati.'),
 'cad_drawing_silhouette': ('drawing',
                            'Calcola i contorni apparenti delle superfici nella vista di tavola.'),
 'cad_drawing_style.h': ('drawing', 'Stili grafici e convenzioni di presentazione delle tavole.'),
 'cad_drawing_text_layout.h': ('drawing', 'Misura e disposizione del testo delle annotazioni sul foglio.'),
 'cad_drawing_tolerances.h': ('drawing', 'Presentazione delle quote: tolleranze, prefissi/suffissi, frecce e flag di esclusione individuale del badge.'),
 'cad_drawing_toolbars.h': ('drawing', 'Costruzione e configurazione delle toolbar della suite 2D.'),
 'cad_drawing_visibility': ('drawing',
                            'Classifica la visibilità con indice spaziale XY degli occludenti analitici (sfere, cilindri, coni, tori), mesh per auto-occlusione e rifilature, tagli parametrici delle coniche.'),
 'cad_drawing_sheets_ui.cpp': ('drawing', 'Schede e gestore dei fogli, identita persistenti, storia globale con ripristino della sorgente quando cambia modello e preparazione su tutti i fogli.'),
 'cad_drawing_window': ('drawing',
                        'Finestra della suite 2D, documento multifoglio, salvataggio e PDF multipagina con formato per foglio.'),
 'cad_element_names': ('features',
                       'Registra genealogia delle feature e nomi topologici persistenti per risolvere i '
                       'riferimenti.'),
 'cad_export': ('io', 'Esporta la geometria del modello nei formati di scambio supportati.'),
 'cad_expression': ('model',
                    'Espressioni e conversioni per documento: contesto unita ereditato dai widget e passato ai formatter, senza stato globale condiviso fra finestre.'),
 'cad_extrude': ('features',
                 'Costruisce estrusioni con condizioni di fine e applica la fusione con i corpi '
                 'selezionati.'),
 'cad_feature_build': ('features',
                       'Costruisce le feature dagli operandi; la soppressione trasferisce il risultato precedente; ripete anche utensili sweep solidi conservando i diagnostici degli operandi.'),
 'cad_feature_dependencies': ('features', 'Dipendenze e operandi delle feature; consumo dei risultati esposti dagli aggregati storici e target delle estensioni.'),
 'cad_features': ('features', 'Tipi e regole comuni delle operazioni parametriche del modello.'),
 'cad_forge': ('features',
               'Adatta sketch e parametri al kernel; helper unico delle booleane con registrazione della genealogia e ordine degli operandi conservato.'),
 'cad_history': ('features', 'Ricostruzione e gestione degli stadi della storia delle feature.'),
 'cad_history_graph': ('features', 'Rappresenta e analizza il grafo delle dipendenze della storia.'),
 'cad_icons': ('startup', 'Icone vettoriali adattate al tema, incluse estensione fino a riferimento, rete di curve e manipolazione dei poli NURBS distinte da loft/spline.'),
 'cad_import': ('io', 'Importa geometria da formati di scambio nel modello applicativo.'),
 'cad_kernel': ('sketch',
                'Sistemi di riferimento degli sketch, trasformazioni e regole comuni di modellazione.'),
 'cad_macos_menu': ('startup', 'Integrazione dei menu con il comportamento nativo di macOS.'),
 'cad_mass': ('analysis', 'Calcola proprietà di massa, area, volume e momenti dei corpi.'),
 'cad_measure': ('analysis', 'Misura distanze e relazioni tra entità sulla geometria esatta, con formattazione nel contesto delle unita del documento.'),
 'forgeCad2026_menu.cpp': ('startup', 'Menu con Fusion esplicito, fondale sfocato, ombra e diagnostica; integrazione viewport nell’app e variante autonoma condivisa dai runner delle tavole.'),
 'forgeCad2026_ui_glass': ('startup', 'Aspetto condiviso di vetro UI: impostazioni, palette, sfocatura CPU e ombre arrotondate memorizzate.'),
 'cad_menu.h': ('startup', 'Popup comuni: palette della finestra, angoli antialias e padding dal raggio dei pannelli, spunte separate dalle icone.'),
 'cad_model_history': ('features',
                       'Gestisce corpi logici riallineati alle basi dopo modifiche e riordini e sequenza autonoma di schizzi e lavorazioni: identita, migrazione, posizione e dipendenze degli appoggi.'),
 'cad_native_revisions': ('documents', 'Persistenza delle revisioni native; confronto delle definizioni normalizzato al codec corrente anche per check-in pendenti in formato53.'),
 'cad_numeric_input.h': ('startup', 'Gestione comune dell’input numerico nei controlli dell’interfaccia.'),
 'cad_overlay_renderer': ('render', 'Disegno OpenGL degli overlay e delle loro istanze.'),
 'tests/test_pattern_placements.h': ('tests', 'Regressioni ripetizioni: posizioni, corpi separati, esclusioni, aggregati legacy, utensili sweep solidi con volumi analitici, dialogo, Undo/Redo e persistenza.'),
 'cad_pattern': ('features', 'Ripetizioni con esclusioni persistenti e idoneita delle feature condivisa tra pannello e builder, inclusi sweep solidi; separa corpi conservando cavita e genealogia.'),
 'cad_revision_client': ('documents',
                         'Protocollo client per copie di lavoro, sessioni e operazioni sulle revisioni.'),
 'cad_revision_client_internal.h': ('documents',
                                    'Helper privati del protocollo revisioni: identità, percorsi, lock e '
                                    'file di sessione.'),
 'cad_revision_store': ('documents', 'Archivio dei check-in, rilasci e checkpoint del catalogo: pubblicazione transazionale, puntatore all ultimo stato, verifica di catene/payload/rilasci e recupero esplicito senza rollback o perdita delle prenotazioni.'),
 'cad_revision_recovery_ui': ('documents', 'Dialogo condiviso 3D/2D per verificare archivio/checkpoint, attivare la protezione legacy e recuperare l ultimo catalogo registrato conservando il file danneggiato.'),
 'tests/test_catalog_recovery.cpp': ('tests', 'Checkpoint/proiezione, interruzioni, recupero atomico/idempotente, concorrenza, token, rilasci, conflitti, corruzione e migrazione legacy.'),
 'tests/test_catalog_recovery_gui.h': ('tests', 'Recupero nativo dai menu3D/2D: annulla, catalogo corrotto/assente, legacy, sessioni/pendenti conservati e consegna storica multipagina.'),
 'cad_shape_analysis': ('analysis', 'Campiona e visualizza analisi di curvatura e qualità delle forme.'),
 'cad_sketch_edit': ('sketch', 'Operazioni di modifica della geometria dello sketch.'),
 'cad_sketch_offset': ('sketch', 'Costruzione degli offset delle entità dello sketch.'),
 'cad_sketch_refs': ('sketch', 'Riferimenti associativi dello sketch a geometrie e feature del modello.'),
 'cad_snap': ('sketch', 'Ricerca degli agganci geometrici per disegno e selezione.'),
 'cad_snapshot_chunks': ('documents',
                         'Suddivide e ricompone snapshot per la conservazione dei dati documentali.'),
 'cad_spatial_sketch': ('sketch', 'Gestisce entità e riferimenti degli sketch spaziali.'),
 'cad_toolbar.h': ('startup', 'Componente toolbar personalizzato dell’interfaccia.'),
 'cad_topology_ref': ('features',
                      'Crea e risolve riferimenti a facce, spigoli e vertici, anche dopo la '
                      'rigenerazione.'),
 'cad_types.h': ('model',
                 'Tipi applicativi condivisi: sketch, entità, feature, display, riferimenti e stato del '
                 'documento.'),
 'cad_workspace': ('documents',
                   'Individua e inizializza il workspace del documento e le cartelle delle revisioni.'),
 'cuda_support.cu': ('analysis', 'Implementazione CUDA dei calcoli accelerati disponibili.'),
 'cuda_support.h': ('analysis', 'Contratto comune dei percorsi CUDA e CPU.'),
 'cuda_support_cpu.cpp': ('analysis',
                          'Implementazione CPU del contratto di accelerazione quando CUDA non è '
                          'disponibile.'),
 'forgeCad2026_glass_compositor': ('render',
                                   'Compositore dei fondali sfocati dei pannelli, con maschera arrotondata '
                                   'analitica e antialias per pixel, shader e framebuffer propri.'),
 'kernel/CMakeLists.txt': ('build', 'Definisce libreria geometrica e target di verifica del kernel.')})

TOKEN_DESCRIPTIONS = {
 'viewport':'Vista e interazione', 'constraint':'Vincoli geometrici', 'solver':'Risoluzione numerica',
 'sketch':'Sketch', 'curve':'Curve', 'surface':'Superfici', 'brep':'Rappresentazione B-rep',
 'boolean':'Operazioni booleane', 'tessellat':'Tessellazione', 'triang':'Triangolazione',
 'topology':'Topologia', 'feature':'Feature parametriche', 'dependenc':'Dipendenze',
 'rebuild':'Ricostruzione', 'history':'Storia delle operazioni', 'document':'Documento',
 'revision':'Revisioni', 'recovery':'Recupero', 'drawing':'Tavole 2D', 'section':'Sezioni',
 'dimension':'Quotatura', 'export':'Esportazione', 'import':'Importazione', 'step':'Formato STEP',
 'stl':'Formato STL', 'iges':'Formato IGES', 'mesh':'Mesh', 'display':'Dati di visualizzazione',
 'render':'Rendering', 'selection':'Selezione', 'pick':'Picking', 'snap':'Snap', 'thread':'Filettature',
 'sweep':'Sweep', 'loft':'Loft', 'extrusion':'Estrusione', 'fillet':'Raccordi', 'chamfer':'Smussi',
 'analysis':'Analisi geometrica', 'shape':'Forme geometriche', 'datum':'Riferimenti geometrici',
 'transform':'Trasformazioni', 'pattern':'Ripetizioni', 'offset':'Offset', 'trim':'Taglio geometrico',
 'sew':'Cucitura', 'thicken':'Ispessimento', 'draft':'Sformo', 'primitive':'Primitive',
 'font':'Font', 'text':'Testo', 'gpu':'Accelerazione GPU', 'cuda':'Calcolo CUDA',
 'nurbs':'NURBS', 'intersect':'Intersezioni', 'bounding':'Limiti geometrici', 'projection':'Proiezioni'}


def inventory():
    names = subprocess.check_output(['git','ls-files','--cached','--others','--exclude-standard','-z'], cwd=ROOT).decode().split('\0')
    suffixes = {'.cpp','.h','.hpp','.c','.cu','.cuh','.mm','.py','.sh','.cmake','.qrc','.vert','.frag'}
    return sorted({n for n in names if n and (Path(n).suffix in suffixes or Path(n).name=='CMakeLists.txt')
                   and not n.startswith(('docs/architecture/', 'build', 'forgecad-release/'))
                   and (ROOT/n).is_file()})


def digest(path):
    return hashlib.sha256((ROOT/path).read_bytes()).hexdigest()


def classify(path):
    p=Path(path);stem=p.stem;low=path.lower()
    if path in DESCRIPTIONS:return DESCRIPTIONS[path]
    if stem in DESCRIPTIONS:return DESCRIPTIONS[stem]
    if path.startswith(('tests/','kernel/tests/')) or stem.startswith('test_'):group='tests'
    elif path.startswith('kernel/'):group='kernel'
    elif path.startswith(('tools/','packaging/')) or p.suffix in {'.py','.sh','.cmake','.qrc'} or p.name=='CMakeLists.txt':group='build'
    elif any(w in low for w in ('drawing','dimension','section_view')):group='drawing'
    elif any(w in low for w in ('document','revision','recovery','metadata','access','snapshot','persistence')):group='documents'
    elif any(w in low for w in ('import','export','step_','stl_','iges','pdf','dxf')):group='io'
    elif any(w in low for w in ('display','render','shader','camera','opengl','gpu_pick')):group='render'
    elif any(w in low for w in ('sketch','constraint','solver','snap','curve_edit')):group='sketch'
    elif any(w in low for w in ('mesh','analysis','analyz','cuda')):group='analysis'
    elif any(w in low for w in ('feature','rebuild','history','extrusion','sweep','loft','boolean','chamfer','fillet','pattern','datum','primitive','offset','thicken','draft','trim')):group='features'
    else:group='model'
    if group=='kernel':
        header=p if p.suffix in {'.h','.hpp'} else p.with_suffix('.h')
        if (ROOT/header).is_file():
            comments=[];started=False
            for line in (ROOT/header).read_text(errors='replace').splitlines():
                text=line.strip()
                if text.startswith('//'):
                    comments.append(text[2:].strip());started=True
                elif started:break
            if comments:
                paragraph=' '.join(comments)
                first=re.split(r'(?<=[.!?])\s',paragraph,maxsplit=1)[0]
                return group, first if len(first)<=290 else first[:287]+'…'
    themes=list(dict.fromkeys(v for k,v in TOKEN_DESCRIPTIONS.items() if k in low))
    role='Verifica' if group=='tests' else ('Contratti e tipi' if p.suffix in {'.h','.hpp','.cuh'} else 'Implementazione')
    # Nome preciso sempre presente: evita descrizioni generiche indistinguibili.
    description=f'{role}: '+(' / '.join(themes[:3]) if themes else stem.replace('_',' '))+'.'
    return group, description


def local_includes(files):
    known=set(files);by_name={}
    for f in files:by_name.setdefault(Path(f).name,[]).append(f)
    edges=[]
    for f in files:
        for line,text in enumerate((ROOT/f).read_text(errors='replace').splitlines(),1):
            m=re.match(r'\s*#\s*include\s*["<]([^">]+)[">]',text)
            if not m:continue
            name=m[1];candidates=[posixpath.normpath((Path(f).parent/name).as_posix()),name,'kernel/'+name]
            target=next((c for c in candidates if c in known),None)
            if not target and len(by_name.get(name,[]))==1:target=by_name[name][0]
            if target:edges.append((f,target,'include',line,name,''))
    return edges


def parse_unit(job):
    # Worker indipendente: nessun oggetto libclang condiviso fra processi.
    import clang.cindex as c
    filename,args,root,known=job;root=Path(root);known=set(known)
    idx=c.Index.create();tu=idx.parse(filename,args=args)
    errors=[str(d).replace(str(root)+'/', '') for d in tu.diagnostics if d.severity>=3]
    def relative(loc):
        if not loc.file:return None
        try:p=Path(loc.file.name).resolve().relative_to(root).as_posix()
        except ValueError:return None
        return p if p in known else None
    def qualified(cur):
        parts=[cur.spelling];p=cur.semantic_parent
        while p and p.kind!=c.CursorKind.TRANSLATION_UNIT:
            if p.spelling:parts.append(p.spelling)
            p=p.semantic_parent
        return '::'.join(reversed(parts))
    functions={c.CursorKind.FUNCTION_DECL,c.CursorKind.CXX_METHOD,c.CursorKind.CONSTRUCTOR,c.CursorKind.DESTRUCTOR,c.CursorKind.CONVERSION_FUNCTION,c.CursorKind.FUNCTION_TEMPLATE}
    definitions={};calls=[];symbols={}
    def visit(cur,owner=''):
        file=relative(cur.location)
        if not file:
            if cur.kind==c.CursorKind.TRANSLATION_UNIT:
                for child in cur.get_children():visit(child)
            return
        if cur.kind in functions and cur.is_definition():
            owner=qualified(cur);usr=cur.get_usr()
            if usr:definitions[usr]=(file,owner,cur.location.line)
            symbols.setdefault(file,set()).add(owner)
        if cur.kind==c.CursorKind.CALL_EXPR:
            ref=cur.referenced
            if ref and ref.kind in functions and relative(ref.location):
                calls.append((file,ref.get_usr(),relative(ref.location),'call',cur.location.line,qualified(ref),owner))
        elif cur.kind==c.CursorKind.UNARY_OPERATOR:
            tokens=cur.get_tokens();first=next(tokens,None)
            if first and first.spelling=='&':
                for child in cur.get_children():
                    ref=child.referenced
                    if ref and ref.kind in functions and relative(ref.location):
                        calls.append((file,ref.get_usr(),relative(ref.location),'reference',cur.location.line,qualified(ref),owner))
        for child in cur.get_children():visit(child,owner)
    visit(tu.cursor)
    return {'file':str(Path(filename).relative_to(root)), 'errors':errors, 'definitions':definitions, 'calls':calls,
            'symbols':{k:sorted(v) for k,v in symbols.items()}}


def compilation_jobs(db,files,resource):
    records=json.loads(db.read_text());known=set(files);selected={}
    # libclang non eredita i percorsi impliciti del driver Apple c++ usato
    # dalla build. libc++ deve precedere gli header C/builtin (stddef.h).
    sdk=None
    if sys.platform=='darwin':
        sdk=Path(subprocess.check_output(['xcrun','--show-sdk-path'],text=True).strip())
    for record in records:
        path=Path(record['file']).resolve()
        try:rel=path.relative_to(ROOT).as_posix()
        except ValueError:continue
        if rel not in known or path.suffix not in {'.cpp','.c','.mm'} or rel in selected:continue
        args=record.get('arguments') or shlex.split(record['command']);args=args[1:];clean=[];i=0
        while i<len(args):
            a=args[i]
            if a in {'-o','-MF','-MT','-MQ'}:i+=2;continue
            if a in {'-c','-MD','-MMD','-MP'} or a==str(path) or a=='-mno-direct-extern-access':i+=1;continue
            if a in {'-I','-isystem','-iquote','-include'} and i+1<len(args):
                v=args[i+1];v=str((Path(record['directory'])/v).resolve()) if not os.path.isabs(v) else v
                clean.extend([a,v]);i+=2;continue
            if a.startswith('-I') and len(a)>2 and not os.path.isabs(a[2:]):a='-I'+str((Path(record['directory'])/a[2:]).resolve())
            clean.append(a);i+=1
        if sdk:
            if '-isysroot' not in clean and not any(a.startswith('--sysroot=') for a in clean):
                clean+=['-isysroot',str(sdk)]
            cxx=sdk/'usr/include/c++/v1'
            if cxx.is_dir():clean+=['-isystem',str(cxx)]
        if resource:clean+=['-isystem',resource]
        clean+=['-I'+str(ROOT),'-Wno-unknown-attributes']
        selected[rel]=(str(path),clean,str(ROOT),files)
    return list(selected.values())


def aggregate(records):
    result={}
    for source,target,kind,line,symbol,owner in records:
        key=(source,target,kind)
        edge=result.setdefault(key,{'source':source,'target':target,'kind':kind,'count':0,'sites':[]})
        site={'line':line,'symbol':symbol,'owner':owner}
        # Duplicati AST di template/header in TUs diverse non gonfiano i conteggi.
        ident=(line,symbol,owner)
        edge.setdefault('_seen',set()).add(ident)
    for edge in result.values():
        sites=sorted(edge.pop('_seen'));edge['count']=len(sites)
        edge['sites']=[{'line':l,'symbol':s,'owner':o} for l,s,o in sites]
    return sorted(result.values(),key=lambda e:(e['source'],e['target'],e['kind']))


def render(graph):
    data=json.dumps(graph,ensure_ascii=False,separators=(',',':')).replace('<','\\u003c')
    return (OUT/'template.html').read_text().replace('/*__GRAPH__*/',data)


def check(quiet=False):
    graph=json.loads((OUT/'graph.json').read_text());files=inventory();expected=graph['hashes']
    actual={f:digest(f) for f in files};actual['docs/architecture/template.html']=digest('docs/architecture/template.html')
    changed=sorted(f for f in set(actual)|set(expected) if actual.get(f)!=expected.get(f))
    assert not changed,'Vista obsoleta; rigenerare. File: '+', '.join(changed[:30])
    known={n['id'] for n in graph['nodes']}
    assert known==set(files),'Catalogo incompleto'
    assert all(e['source'] in known and e['target'] in known for e in graph['edges']),'Collegamenti non validi'
    assert (OUT/'index.html').read_text()==render(graph),'HTML diverso dal grafo o template'
    assert not graph['analysis']['errors'],'Analisi C++ incompleta; risolvere diagnostica prima di dichiararla verificata'
    if not quiet:print(f"PASS: {len(files)} file, {len(graph['edges'])} relazioni; contenuti e vista aggiornati.")


@contextmanager
def generation_lock():
    # Stesso lock per checkout, anche se due build diverse aggiornano la vista.
    # Linux/macOS: lock del processo, rilasciato dal sistema anche dopo un crash.
    import fcntl
    name='forgecad-architecture-'+hashlib.sha256(str(ROOT).encode()).hexdigest()[:20]+'.lock'
    with (Path(tempfile.gettempdir())/name).open('a') as handle:
        fcntl.flock(handle,fcntl.LOCK_EX)
        try:yield
        finally:fcntl.flock(handle,fcntl.LOCK_UN)


def write_changed(path,content):
    if path.exists() and path.read_text()==content:return
    temporary=None
    try:
        with tempfile.NamedTemporaryFile(mode='w',encoding='utf-8',dir=path.parent,
                                         prefix='.'+path.name+'.',delete=False) as handle:
            temporary=Path(handle.name);handle.write(content)
        temporary.replace(path)
    finally:
        if temporary and temporary.exists():temporary.unlink()


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build-dir',type=Path);p.add_argument('--check',action='store_true');p.add_argument('--update',action='store_true',help='Aggiorna solo se sorgenti, vista o configurazione sono cambiati');p.add_argument('--jobs',type=int,default=2);p.add_argument('--clang-resource-dir',help='Directory include builtin di clang, se non rilevata automaticamente')
    args=p.parse_args()
    if args.check:check();return
    if not args.build_dir:p.error('Specificare --build-dir con compile_commands.json')
    db=args.build_dir.resolve()/'compile_commands.json'
    if not db.exists():p.error('Manca compile_commands.json: configurare CMake con -DCMAKE_EXPORT_COMPILE_COMMANDS=ON')
    write_changed(ROOT/'.forgecad-architecture.json',json.dumps({'buildDir':str(args.build_dir.resolve())},indent=2)+'\n')
    configuration_digest=hashlib.sha256(db.read_bytes()).hexdigest()
    if args.update:
        try:
            check(quiet=True)
            previous=json.loads((OUT/'graph.json').read_text())
            if previous['analysis'].get('configurationDigest')==configuration_digest:
                print('Architettura già aggiornata.');return
        except (AssertionError,OSError,ValueError,KeyError):pass
        print('Aggiornamento automatico della mappa architetturale…',flush=True)
    try:import clang.cindex  # noqa: F401
    except ImportError:p.error('Installare i binding Python clang compatibili con libclang (per esempio python3-clang su Linux)')
    resource=args.clang_resource_dir
    if not resource:
        try:resource=str(Path(subprocess.check_output(['clang','-print-resource-dir'],text=True).strip())/'include')
        except (OSError,subprocess.CalledProcessError):
            roots=sorted(Path('/usr/lib/clang').glob('*/include'),key=lambda x:[int(n) for n in re.findall(r'\d+',str(x))]);resource=str(roots[-1]) if roots else None
    files=inventory();nodes=[{'id':f,'group':classify(f)[0],'description':classify(f)[1],'lines':len((ROOT/f).read_bytes().splitlines())} for f in files]
    records=local_includes(files);jobs=compilation_jobs(db,files,resource);results=[]
    cache_dir=args.build_dir.resolve()/'architecture-cache';cache_dir.mkdir(exist_ok=True)
    headers=hashlib.sha256(''.join(digest(f) for f in files if Path(f).suffix in {'.h','.hpp','.cuh'}).encode()).hexdigest()
    pending=[]
    for job in jobs:
        key=hashlib.sha256((str(VERSION)+headers+Path(job[0]).read_text(errors='replace')+json.dumps(job[:2])).encode()).hexdigest()
        cache=cache_dir/(key+'.json')
        if cache.exists() and not json.loads(cache.read_text()).get('errors'):
            results.append(json.loads(cache.read_text()))
        else:pending.append((job,cache))
    print(f'{len(results)} unità da cache, {len(pending)} da analizzare.',flush=True)
    with concurrent.futures.ProcessPoolExecutor(max_workers=max(1,args.jobs)) as pool:
        for (_,cache),result in zip(pending,pool.map(parse_unit,[j for j,c in pending],chunksize=1)):
            if not result['errors']:write_changed(cache,json.dumps(result))
            results.append(result);print(f"[{len(results)}/{len(jobs)}] {result['file']}"+(' ERRORI' if result['errors'] else ''),flush=True)
    definitions={};symbols={}
    for r in results:
        definitions.update(r['definitions'])
        for f,syms in r['symbols'].items():symbols.setdefault(f,set()).update(syms)
    for r in results:
        for source,usr,decl,kind,line,symbol,owner in r['calls']:
            target=definitions.get(usr,(decl,))[0];records.append((source,target,kind,line,symbol,owner))
    for node in nodes:node['symbols']=sorted(symbols.get(node['id'],[]))
    errors={r['file']:r['errors'] for r in results if r['errors']}
    hashes={f:digest(f) for f in files};hashes['docs/architecture/template.html']=digest('docs/architecture/template.html')
    graph={'version':VERSION,'groups':[{'id':a,'name':b,'description':c} for a,b,c in GROUPS], 'nodes':nodes,'edges':aggregate(records),'hashes':hashes,
           'analysis':{'configurationDigest':configuration_digest,'engine':'libclang: AST della configurazione CMake indicata', 'units':sorted(r['file'] for r in results),'errors':errors,
           'limits':'Chiamate statiche risolte e indirizzi di funzione nel codice C++ compilato. Include locali rilevati testualmente, anche nei rami condizionali. Dispatch virtuale, segnali/slot dinamici, std::function, plugin e rami esclusi dalla configurazione possono aggiungere chiamate a runtime. CUDA non analizzato via AST; relazioni include disponibili. Le frecce fra file della stessa classe possono rappresentare chiamate a metodi del medesimo oggetto.'}}
    if errors:
        for filename,diagnostics in errors.items():
            for diagnostic in diagnostics:print(filename+': '+diagnostic,file=sys.stderr)
        print('Analisi incompleta: conservata la vista precedente.',file=sys.stderr)
        sys.exit(1)
    write_changed(OUT/'graph.json',json.dumps(graph,ensure_ascii=False,indent=2)+'\n')
    write_changed(OUT/'index.html',render(graph))
    print(f"Generati {len(nodes)} file, {len(graph['edges'])} relazioni, {len(errors)} TUs con diagnostica.")
    if errors:sys.exit(1)

if __name__=='__main__':
    try:
        with generation_lock():main()
    except (AssertionError,FileNotFoundError) as error:print(str(error),file=sys.stderr);sys.exit(1)
