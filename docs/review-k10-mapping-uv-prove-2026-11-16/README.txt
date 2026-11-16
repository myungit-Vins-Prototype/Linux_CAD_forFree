K10 Mapping/Quilt UV — prove del 16 novembre 2026

SHA256SUMS inventaria il pacchetto effettivo. Verificare con:
  cd docs/review-k10-mapping-uv-prove-2026-11-16
  sha256sum -c SHA256SUMS

root/: baseline4232hash, sorgenti prima/finali, differenza fase, build/CTest,
corpiRelease verificati via SHA (i binari CAD non sono inclusi), Wayland,
avvioapp, audit93oggettiKernel (solo2differenti), architettura e preservazione.
core/: soli due sorgenti modificati del candidatoV3strict, patch/hash/attribuzione. V2 non integrato.
tests/: permanente rigoroso e baseline_compat privata distinta, log e script
Release/sanitizer. Nessuna macro di skip nel test permanente.
independent/:20exportJSONV3/80fin, dati esatti IEEE in hex, script Fraction,
risultati e negativo alterato. Certificati80fin/27089campate complete.

Comandi su build propria:
  cmake --build forgecad-release --parallel 2
  ctest --test-dir forgecad-release --output-on-failure -R 'forgekernel_(mapping|surface_mapping|planar_flatten)'
  ./forgecad-release/kernel/forgekernel_mapping_uv_validation_tests
Il runner accetta una directory di export opzionale, usando solo dati generati.
I comandi privati degli agenti conservano percorsi /tmp originali: adattarli
in una copia sacrificabile, mai sostituire sorgenti nel checkout di lavoro.

I manifesti degli agenti attribuiscono anche hash di binari/oggetti privati:
non equivalgono all'inventario del pacchetto. ELF, oggetti, librerie, .body/.prt
esclusi. I sorgenti sono testo inerte .cpp.txt/.h.txt/.py.txt/.sh.txt;
CMakeLists.txt.txt è intenzionale. Il JSON include i coefficienti necessari
alla verifica indipendente senza usare il provider C++.

La baseline diagnostica privata omette solo la nuova asserzione del provider
continuo:19PASS/2FAIL geometrici reali aUV1e4. Il CTest permanente la richiede
sempre. Le18varianti restanti erano già compatibili. UV1e5/1e6 sono rifiuti
conservativi atomici della V3, non successi dichiarati. Il primo log LSan
in sandbox è fermato da ptrace; stesso binario fuori ptrace21PASS.

Limiti: fin Plane finale certificata, bordo3D e mappa generale campionati;
nessuna certificazione di trim/iniettività/volume/autointersezioni. Sanitizer
solo driver+Mapping+Quilt+bounds, restoRelease. Nessuna garanzia globale leak.
