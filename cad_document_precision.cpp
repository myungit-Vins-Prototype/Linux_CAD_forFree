#include "cad_document_precision.h"
#include "forgeCad2026_viewport.h"
#include "fk_precision.h"
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>

double CadViewport::calculationAccuracy() const { return calculationAccuracy_; }

QString CadViewport::setCalculationAccuracy(double accuracy) {
    if (documentReadOnly_) return QStringLiteral("Documento in sola lettura: eseguire il check-out.");
    if (!ForgeCad::Kernel::validCalculationAccuracy(accuracy)) return QStringLiteral("Precisione fuori intervallo.");
    if (interactionLocked_ || responsiveWork_) return QStringLiteral("Attendere la fine del calcolo corrente.");
    if (accuracy == calculationAccuracy_) return {};
    if (sketchMode_) endSketchMode();
    clearPreview();
    recordUndo();
    calculationAccuracy_ = accuracy;
    for (auto &body : extrusions_) body.cachedGeometry = false;
    regenerateAll();
    documentChanged();
    int failed = 0;
    for (const auto &body : extrusions_) if (!body.suppressed && !body.error.isEmpty()) ++failed;
    return failed ? QStringLiteral("Precisione impostata; %1 funzioni non sono riuscite nel ricalcolo. Annulla ripristina lo stato precedente.").arg(failed) : QString();
}

namespace ForgeCad {
void editDocumentPrecision(QWidget *parent, CadViewport *viewport) {
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("documentPrecisionDialog"));
    dialog.setWindowTitle(QStringLiteral("Precisione di calcolo del documento"));
    auto *form = new QFormLayout(&dialog);
    auto *preset = new QComboBox(&dialog);
    preset->setObjectName(QStringLiteral("documentPrecisionPreset"));
    preset->addItem(QStringLiteral("Standard — 1e-7 mm"), 1e-7);
    preset->addItem(QStringLiteral("Alta — 1e-8 mm"), 1e-8);
    preset->addItem(QStringLiteral("Molto alta — 1e-9 mm"), 1e-9);
    preset->addItem(QStringLiteral("Personalizzata"), 0.);
    auto *accuracy = new QDoubleSpinBox(&dialog);
    accuracy->setObjectName(QStringLiteral("documentPrecisionAccuracy"));
    accuracy->setDecimals(12); accuracy->setRange(1e-10, 1e-7);
    accuracy->setSingleStep(1e-9); accuracy->setValue(viewport->calculationAccuracy());
    accuracy->setSuffix(QStringLiteral(" mm"));
    int chosen = 3;
    for (int i=0;i<3;++i) if (preset->itemData(i).toDouble()==viewport->calculationAccuracy()) chosen=i;
    preset->setCurrentIndex(chosen);
    form->addRow(QStringLiteral("Profilo:"), preset);
    form->addRow(QStringLiteral("Risoluzione di riferimento:"), accuracy);
    auto *note = new QLabel(QStringLiteral("Un valore più piccolo richiede calcoli più accurati e può aumentare tempo e memoria. "
        "La conferma ricalcola le funzioni del documento. La qualità della mesh è indipendente; "
        "la geometria importata conserva la precisione della sorgente."), &dialog);
    note->setWordWrap(true); note->setMaximumWidth(480); form->addRow(note);
    QObject::connect(preset, &QComboBox::currentIndexChanged, &dialog, [=](int index) {
        const auto value=preset->itemData(index).toDouble(); if (value>0) accuracy->setValue(value);
    });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
    QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec()!=QDialog::Accepted) return;
    const auto error=viewport->setCalculationAccuracy(accuracy->value());
    if (!error.isEmpty()) QMessageBox::warning(parent,QStringLiteral("Precisione del documento"),error);
}
}
